#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#ifndef RWF_DONTCACHE
#define RWF_DONTCACHE 0x00000080
#endif

struct sample {
	uint64_t sequence_ns;
	uint64_t classify_ns;
	uint64_t cleanup_ns;
	uint64_t total_ns;
	uint64_t polls;
};

static uint64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static ssize_t probe(int fd, int flags)
{
	char byte;
	struct iovec iov = {.iov_base = &byte, .iov_len = 1};
	return preadv2(fd, &iov, 1, 0, flags);
}

static int page_cached(void *map, long page_size)
{
	unsigned char vec = 0;
	if (mincore(map, page_size, &vec) == -1)
		return -1;
	return !!(vec & 1);
}

static int drop_page(int fd)
{
	return posix_fadvise(fd, 0, 4096, POSIX_FADV_DONTNEED);
}

static int stabilize_absent(int fd, void *map, long page_size)
{
	struct timespec settle = {.tv_sec = 0, .tv_nsec = 50000};
	if (drop_page(fd) != 0)
		return -1;
	nanosleep(&settle, NULL);
	if (drop_page(fd) != 0)
		return -1;
	return page_cached(map, page_size) == 0 ? 0 : -1;
}

static int wait_for_hit(int fd, int flags, uint64_t *polls)
{
	for (*polls = 0; *polls < 10000000; ++*polls) {
		errno = 0;
		ssize_t ret = probe(fd, flags);
		if (ret == 1) {
			++*polls;
			return 0;
		}
		if (ret != -1 || errno != EAGAIN)
			return -1;
	}
	errno = ETIMEDOUT;
	return -1;
}

static int sample_dontcache(int fd, void *map, long page_size,
			    struct sample *s)
{
	uint64_t sequence_start = now_ns();
	if (drop_page(fd) != 0)
		return -1;
	uint64_t start = now_ns();
	errno = 0;
	ssize_t ret = probe(fd, RWF_NOWAIT | RWF_DONTCACHE);
	uint64_t classified = now_ns();
	if (ret != -1 || errno != EAGAIN) {
		fprintf(stderr, "DONTCACHE initial probe: ret=%zd errno=%d cache=%d\n",
			ret, errno, page_cached(map, page_size));
		return -1;
	}
	if (wait_for_hit(fd, RWF_NOWAIT | RWF_DONTCACHE, &s->polls) == -1)
		return -1;
	uint64_t end = now_ns();
	s->classify_ns = classified - start;
	s->cleanup_ns = end - classified;
	s->total_ns = end - start;
	s->sequence_ns = end - sequence_start;
	return page_cached(map, page_size) == 0 ? 0 : 1;
}

static int sample_monitor_flush(int fd, void *map, long page_size,
				struct sample *s)
{
	uint64_t sequence_start = now_ns();
	if (drop_page(fd) != 0)
		return -1;
	uint64_t start = now_ns();
	errno = 0;
	ssize_t ret = probe(fd, RWF_NOWAIT);
	uint64_t classified = now_ns();
	if (ret != -1 || errno != EAGAIN) {
		fprintf(stderr, "Monitor initial probe: ret=%zd errno=%d cache=%d\n",
			ret, errno, page_cached(map, page_size));
		return -1;
	}
	if (wait_for_hit(fd, RWF_NOWAIT, &s->polls) == -1)
		return -1;
	if (drop_page(fd) != 0)
		return -1;
	uint64_t end = now_ns();
	s->classify_ns = classified - start;
	s->cleanup_ns = end - classified;
	s->total_ns = end - start;
	s->sequence_ns = end - sequence_start;
	return page_cached(map, page_size) == 0 ? 0 : 1;
}

static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a;
	uint64_t y = *(const uint64_t *)b;
	return (x > y) - (x < y);
}

static void report_metric(const char *name, const struct sample *samples,
			  size_t n, size_t member_offset)
{
	uint64_t *values = malloc(n * sizeof(*values));
	long double sum = 0;
	for (size_t i = 0; i < n; ++i) {
		values[i] = *(const uint64_t *)((const char *)&samples[i] +
						 member_offset);
		sum += values[i];
	}
	qsort(values, n, sizeof(*values), cmp_u64);
	printf("%-13s mean=%9.1Lf ns p50=%" PRIu64 " ns p95=%" PRIu64
	       " ns p99=%" PRIu64 " ns max=%" PRIu64 " ns\n",
	       name, sum / n, values[n / 2], values[(n * 95) / 100],
	       values[(n * 99) / 100], values[n - 1]);
	free(values);
}

static void report(const char *name, const struct sample *samples, size_t n)
{
	printf("\n%s (%zu miss cycles)\n", name, n);
	report_metric("full sequence", samples, n,
		      __builtin_offsetof(struct sample, sequence_ns));
	report_metric("classify", samples, n,
		      __builtin_offsetof(struct sample, classify_ns));
	report_metric("cleanup", samples, n,
		      __builtin_offsetof(struct sample, cleanup_ns));
	report_metric("total/window", samples, n,
		      __builtin_offsetof(struct sample, total_ns));
	report_metric("polls", samples, n,
		      __builtin_offsetof(struct sample, polls));
}

static int test_naive_flush_race(int fd, void *map, long page_size, size_t n)
{
	struct timespec delay = {.tv_sec = 0, .tv_nsec = 50000};
	struct timespec settle = {.tv_sec = 0, .tv_nsec = 1000000};
	size_t survived = 0;
	size_t late = 0;
	size_t stale_before_probe = 0;
	for (size_t i = 0; i < n;) {
		int advice = drop_page(fd);
		int before = page_cached(map, page_size);
		if (advice != 0 || before != 0) {
			fprintf(stderr, "race trial %zu setup: fadvise=%d cache=%d\n",
				i, advice, before);
			return -1;
		}
		errno = 0;
		ssize_t ret = probe(fd, RWF_NOWAIT);
		if (ret == 1) {
			++stale_before_probe;
			if (drop_page(fd) != 0)
				return -1;
			nanosleep(&settle, NULL);
			continue;
		}
		if (ret != -1 || errno != EAGAIN) {
			fprintf(stderr, "race trial %zu probe: ret=%zd errno=%d\n",
				i, ret, errno);
			return -1;
		}
		if (drop_page(fd) != 0)
			return -1;
		nanosleep(&delay, NULL);
		int present = page_cached(map, page_size);
		if (present < 0)
			return -1;
		if (present) {
			++survived;
			if (drop_page(fd) != 0)
				return -1;
		}
		nanosleep(&settle, NULL);
		present = page_cached(map, page_size);
		if (present < 0)
			return -1;
		if (present) {
			++late;
			if (drop_page(fd) != 0)
				return -1;
		}
		++i;
	}
	printf("\nImmediate Monitor->Flush race (%zu trials)\n", n);
	printf("cached after 50 us: %zu/%zu (%.2f%%)\n", survived, n,
	       100.0 * survived / n);
	printf("late cache fill by 1 ms: %zu/%zu (%.2f%%)\n", late, n,
	       100.0 * late / n);
	printf("stale fills caught before a trial: %zu\n", stale_before_probe);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: %s FILE ITERATIONS\n", argv[0]);
		return 2;
	}
	size_t n = strtoul(argv[2], NULL, 10);
	if (!n)
		return 2;
	int fd = open(argv[1], O_RDONLY);
	if (fd == -1) {
		perror("open");
		return 1;
	}
	long page_size = sysconf(_SC_PAGESIZE);
	void *map = mmap(NULL, page_size, PROT_NONE, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	struct sample *dontcache = calloc(n, sizeof(*dontcache));
	struct sample *monitor_flush = calloc(n, sizeof(*monitor_flush));
	size_t dontcache_cleanup_failures = 0;
	size_t monitor_flush_cleanup_failures = 0;
	if (!dontcache || !monitor_flush)
		return 1;

	if (drop_page(fd) != 0 || page_cached(map, page_size) != 0) {
		fprintf(stderr, "could not establish the initial absent state\n");
		return 1;
	}
	for (size_t i = 0; i < n; ++i) {
		if (stabilize_absent(fd, map, page_size) == -1)
			return 1;
		int ret = sample_dontcache(fd, map, page_size, &dontcache[i]);
		if (ret == -1) {
			fprintf(stderr, "DONTCACHE sample %zu failed: %s\n", i,
				strerror(errno));
			return 1;
		}
		if (ret == 1) {
			++dontcache_cleanup_failures;
			if (drop_page(fd) != 0 || page_cached(map, page_size) != 0)
				return 1;
		}
		if (stabilize_absent(fd, map, page_size) == -1)
			return 1;
		ret = sample_monitor_flush(fd, map, page_size, &monitor_flush[i]);
		if (ret == -1) {
			fprintf(stderr, "Monitor+Flush sample %zu failed: %s\n", i,
				strerror(errno));
			return 1;
		}
		if (ret == 1) {
			++monitor_flush_cleanup_failures;
			if (drop_page(fd) != 0 || page_cached(map, page_size) != 0)
				return 1;
		}
	}
	report("NOWAIT|DONTCACHE", dontcache, n);
	printf("cleanup failures: %zu/%zu (%.4f%%)\n",
	       dontcache_cleanup_failures, n,
	       100.0 * dontcache_cleanup_failures / n);
	report("NOWAIT then Flush", monitor_flush, n);
	printf("cleanup failures: %zu/%zu (%.4f%%)\n",
	       monitor_flush_cleanup_failures, n,
	       100.0 * monitor_flush_cleanup_failures / n);
	size_t race_n = n < 1000 ? n : 1000;
	if (test_naive_flush_race(fd, map, page_size, race_n) == -1) {
		fprintf(stderr, "immediate Flush race test failed: %s\n",
			strerror(errno));
		return 1;
	}

	free(monitor_flush);
	free(dontcache);
	munmap(map, page_size);
	close(fd);
	return 0;
}
