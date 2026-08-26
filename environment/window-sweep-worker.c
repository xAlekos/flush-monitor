#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
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

static uint64_t now_ns(void)
{
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC, &time);
	return (uint64_t)time.tv_sec * 1000000000 + time.tv_nsec;
}

static int wait_until(uint64_t deadline)
{
	struct timespec time = {
		(time_t)(deadline / 1000000000), (long)(deadline % 1000000000)
	};
	int error;
	do {
		error = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &time, NULL);
	} while (error == EINTR);
	return error;
}

static ssize_t probe(int fd, int flags)
{
	char byte;
	struct iovec iov = {&byte, 1};
	return preadv2(fd, &iov, 1, 0, flags);
}

static int flush(int fd, size_t size)
{
	return posix_fadvise(fd, 0, size, POSIX_FADV_DONTNEED);
}

static int cached(void *page, size_t size)
{
	unsigned char state = 0;
	if (mincore(page, size, &state))
		return -1;
	return state & 1;
}

static int wait_for_fill(int fd, int flags)
{
	ssize_t result;
	do {
		result = probe(fd, flags);
	} while (result == -1 && errno == EAGAIN);
	return result == 1 ? 0 : -1;
}

static int attacker(const char *mode, const char *path)
{
	size_t size = sysconf(_SC_PAGESIZE);
	int fd = open(path, O_RDONLY);
	int flags = RWF_NOWAIT |
		(!strcmp(mode, "dontcache") ? RWF_DONTCACHE : 0);
	char line[128];
	if (fd == -1)
		return 1;
	puts("READY");

	while (fgets(line, sizeof(line), stdin)) {
		uint64_t trial, flush_at, monitor_at;
		sscanf(line, "%" SCNu64 " %" SCNu64 " %" SCNu64,
		       &trial, &flush_at, &monitor_at);
		int error = wait_until(flush_at);
		uint64_t flush_start = now_ns();
		if (!error)
			error = flush(fd, size);
		uint64_t flush_end = now_ns();

		if (!error)
			error = wait_until(monitor_at);
		uint64_t monitor_start = now_ns();
		int prediction = -1;
		if (!error) {
			ssize_t result = probe(fd, flags);
			if (result == 1)
				prediction = 1;
			else if (result == -1 && errno == EAGAIN)
				prediction = 0;
			else
				error = errno ? errno : EIO;
		}

		if (!error && prediction == 0)
			error = wait_for_fill(fd, RWF_NOWAIT);
		if (!error)
			error = flush(fd, size);
		uint64_t cleanup_end = now_ns();
		printf("%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
		       ",%d,%" PRIu64 ",%d\n",
		       trial, flush_start, flush_end, monitor_start, prediction,
		       cleanup_end, error);
	}
	return 0;
}

static int victim(const char *path)
{
	size_t size = sysconf(_SC_PAGESIZE);
	int fd = open(path, O_RDONLY);
	void *page = malloc(size);
	char line[128];
	if (fd == -1 || !page)
		return 1;
	puts("READY");

	while (fgets(line, sizeof(line), stdin)) {
		uint64_t trial, event_at;
		int event;
		sscanf(line, "%" SCNu64 " %d %" SCNu64, &trial, &event, &event_at);
		int error = wait_until(event_at);
		uint64_t start = now_ns();
		if (!error && event && pread(fd, page, size, 0) != (ssize_t)size)
			error = errno ? errno : EIO;
		printf("%" PRIu64 ",%d,%" PRIu64 ",%d\n",
		       trial, event, start, error);
	}
	return 0;
}

static int validator(const char *path)
{
	size_t size = sysconf(_SC_PAGESIZE);
	int fd = open(path, O_RDONLY);
	void *page = mmap(NULL, size, PROT_NONE, MAP_SHARED, fd, 0);
	char line[128], phase;
	if (fd == -1 || page == MAP_FAILED)
		return 1;
	puts("READY");

	while (fgets(line, sizeof(line), stdin)) {
		uint64_t trial, when;
		sscanf(line, " %c %" SCNu64 " %" SCNu64, &phase, &trial, &when);
		int error = when ? wait_until(when) : 0;
		uint64_t start = now_ns();
		int state = error ? -1 : cached(page, size);
		if (state == -1 && !error)
			error = errno;
		uint64_t end = now_ns();
		printf("%c,%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%d,%d\n",
		       phase, trial, start, end, state, error);
	}
	return 0;
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc == 4 && !strcmp(argv[1], "attacker"))
		return attacker(argv[2], argv[3]);
	if (argc == 3 && !strcmp(argv[1], "victim"))
		return victim(argv[2]);
	if (argc == 3 && !strcmp(argv[1], "validator"))
		return validator(argv[2]);
	fprintf(stderr, "usage: %s attacker MODE FILE | victim FILE | validator FILE\n",
		argv[0]);
	return 2;
}
