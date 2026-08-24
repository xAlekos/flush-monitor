#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#ifndef RWF_DONTCACHE
#define RWF_DONTCACHE 0x00000080
#endif

enum mode {
	MODE_REFERENCE,
	MODE_FLUSH,
	MODE_DONTCACHE,
};

enum initial_state {
	STATE_MISS,
	STATE_HIT,
};

struct sample {
	uint64_t classify_ns;
	uint64_t cleanup_ns;
	uint64_t total_ns;
	uint64_t polls;
	int cleanup_ok;
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

static int drop_page(int fd, long page_size)
{
	int ret = posix_fadvise(fd, 0, page_size, POSIX_FADV_DONTNEED);
	if (ret)
		errno = ret;
	return ret ? -1 : 0;
}

static int prepare_state(int fd, void *map, long page_size,
			 enum initial_state state)
{
	char byte;
	struct timespec settle = {.tv_sec = 0, .tv_nsec = 50000};

	for (int attempt = 0; attempt < 100; ++attempt) {
		if (drop_page(fd, page_size) == -1)
			return -1;
		if (page_cached(map, page_size) == 0)
			break;
		if (attempt == 99) {
			errno = EBUSY;
			return -1;
		}
		nanosleep(&settle, NULL);
	}

	if (state == STATE_HIT) {
		if (pread(fd, &byte, 1, 0) != 1)
			return -1;
		if (page_cached(map, page_size) != 1) {
			errno = EIO;
			return -1;
		}
	}
	return 0;
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

static int take_sample(enum mode mode, enum initial_state state, int fd,
		       void *map, long page_size, struct sample *sample)
{
	int flags = mode == MODE_DONTCACHE
		? RWF_NOWAIT | RWF_DONTCACHE : RWF_NOWAIT;

	if (prepare_state(fd, map, page_size, state) == -1)
		return -1;

	errno = 0;
	uint64_t start = now_ns();
	ssize_t ret = probe(fd, flags);
	int probe_errno = errno;
	uint64_t classified = now_ns();

	if ((state == STATE_HIT && ret != 1) ||
	    (state == STATE_MISS && (ret != -1 || probe_errno != EAGAIN))) {
		fprintf(stderr, "unexpected classification: state=%s ret=%zd errno=%d\n",
			state == STATE_HIT ? "hit" : "miss", ret, probe_errno);
		errno = EPROTO;
		return -1;
	}

	sample->polls = 0;
	if (state == STATE_HIT) {
		/* Reset to the absent baseline required by the next attack cycle. */
		if (drop_page(fd, page_size) == -1)
			return -1;
	} else if (mode == MODE_FLUSH) {
		/* A modern NOWAIT miss submits I/O: wait before the final Flush. */
		if (wait_for_hit(fd, RWF_NOWAIT, &sample->polls) == -1 ||
		    drop_page(fd, page_size) == -1)
			return -1;
	} else if (mode == MODE_DONTCACHE) {
		/* Consume the completed read; DONTCACHE drops its transient folio. */
		if (wait_for_hit(fd, flags, &sample->polls) == -1)
			return -1;
	}

	uint64_t finished = now_ns();
	sample->classify_ns = classified - start;
	sample->cleanup_ns = finished - classified;
	sample->total_ns = finished - start;
	sample->cleanup_ok = page_cached(map, page_size) == 0;
	return 0;
}

static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a;
	uint64_t y = *(const uint64_t *)b;
	return (x > y) - (x < y);
}

static uint64_t percentile(const struct sample *samples, size_t n,
			   size_t offset, size_t percent)
{
	uint64_t *values = malloc(n * sizeof(*values));
	if (!values)
		return 0;
	for (size_t i = 0; i < n; ++i)
		values[i] = *(const uint64_t *)((const char *)&samples[i] + offset);
	qsort(values, n, sizeof(*values), cmp_u64);
	uint64_t result = values[(n * percent) / 100];
	free(values);
	return result;
}

static void report_metric(const char *name, const struct sample *samples,
			  size_t n, size_t offset)
{
	long double sum = 0;
	for (size_t i = 0; i < n; ++i)
		sum += *(const uint64_t *)((const char *)&samples[i] + offset);
	printf("%-10s mean=%9.1Lf ns p50=%" PRIu64 " ns p95=%" PRIu64
	       " ns p99=%" PRIu64 " ns\n", name, sum / n,
	       percentile(samples, n, offset, 50),
	       percentile(samples, n, offset, 95),
	       percentile(samples, n, offset, 99));
}

static void report_state(const char *state, const struct sample *samples,
			 size_t n)
{
	size_t failures = 0;
	for (size_t i = 0; i < n; ++i)
		failures += !samples[i].cleanup_ok;
	printf("\nstate=%s samples=%zu\n", state, n);
	report_metric("classify", samples, n,
		      __builtin_offsetof(struct sample, classify_ns));
	report_metric("cleanup", samples, n,
		      __builtin_offsetof(struct sample, cleanup_ns));
	report_metric("full-cycle", samples, n,
		      __builtin_offsetof(struct sample, total_ns));
	uint64_t p50 = percentile(samples, n,
				  __builtin_offsetof(struct sample, total_ns), 50);
	uint64_t p95 = percentile(samples, n,
				  __builtin_offsetof(struct sample, total_ns), 95);
	uint64_t p99 = percentile(samples, n,
				  __builtin_offsetof(struct sample, total_ns), 99);
	printf("sampling-hz p50=%.1f p95=%.1f p99=%.1f\n",
	       1e9 / p50, 1e9 / p95, 1e9 / p99);
	printf("cleanup-failures=%zu/%zu (%.4f%%)\n", failures, n,
	       100.0 * failures / n);
}

static int write_csv(const char *path, const char *mode_name,
		     const struct sample *misses, const struct sample *hits,
		     size_t n)
{
	FILE *out = fopen(path, "w");
	if (!out)
		return -1;
	fprintf(out, "mode,state,trial,classify_ns,cleanup_ns,full_cycle_ns,polls,cleanup_ok\n");
	for (size_t i = 0; i < n; ++i) {
		const struct sample *s = &misses[i];
		fprintf(out, "%s,miss,%zu,%" PRIu64 ",%" PRIu64 ",%" PRIu64
			",%" PRIu64 ",%d\n", mode_name, i, s->classify_ns,
			s->cleanup_ns, s->total_ns, s->polls, s->cleanup_ok);
		s = &hits[i];
		fprintf(out, "%s,hit,%zu,%" PRIu64 ",%" PRIu64 ",%" PRIu64
			",%" PRIu64 ",%d\n", mode_name, i, s->classify_ns,
			s->cleanup_ns, s->total_ns, s->polls, s->cleanup_ok);
	}
	return fclose(out);
}

static int parse_mode(const char *name, enum mode *mode)
{
	if (!strcmp(name, "reference"))
		*mode = MODE_REFERENCE;
	else if (!strcmp(name, "flush"))
		*mode = MODE_FLUSH;
	else if (!strcmp(name, "dontcache"))
		*mode = MODE_DONTCACHE;
	else
		return -1;
	return 0;
}

int main(int argc, char **argv)
{
	if (argc != 5) {
		fprintf(stderr, "usage: %s reference|flush|dontcache FILE ITERATIONS RAW.csv\n",
			argv[0]);
		return 2;
	}
	enum mode mode;
	if (parse_mode(argv[1], &mode) == -1)
		return 2;
	size_t n = strtoul(argv[3], NULL, 10);
	if (!n)
		return 2;

	int fd = open(argv[2], O_RDONLY);
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
	struct sample *misses = calloc(n, sizeof(*misses));
	struct sample *hits = calloc(n, sizeof(*hits));
	if (!misses || !hits)
		return 1;

	/* Alternate order to distribute drift equally between both states. */
	for (size_t i = 0; i < n; ++i) {
		enum initial_state first = i & 1 ? STATE_HIT : STATE_MISS;
		enum initial_state second = i & 1 ? STATE_MISS : STATE_HIT;
		struct sample *first_sample = first == STATE_HIT ? &hits[i] : &misses[i];
		struct sample *second_sample = second == STATE_HIT ? &hits[i] : &misses[i];
		if (take_sample(mode, first, fd, map, page_size, first_sample) == -1 ||
		    take_sample(mode, second, fd, map, page_size, second_sample) == -1) {
			fprintf(stderr, "sample %zu failed: %s\n", i, strerror(errno));
			return 1;
		}
	}

	struct utsname uts;
	uname(&uts);
	printf("mode=%s\nkernel=%s\ntarget=%s\npage-size=%ld\ncpu=%d\n",
	       argv[1], uts.release, argv[2], page_size, sched_getcpu());
	report_state("miss", misses, n);
	report_state("hit", hits, n);
	if (write_csv(argv[4], argv[1], misses, hits, n) == -1) {
		perror("write csv");
		return 1;
	}

	free(hits);
	free(misses);
	munmap(map, page_size);
	close(fd);
	return 0;
}
