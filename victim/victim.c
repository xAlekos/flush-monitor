#define _GNU_SOURCE

#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef CONSTANT_ACCESS
#define CONSTANT_ACCESS 0
#endif

static volatile unsigned char sink;

static uint64_t now_ns(void)
{
	struct timespec time;
	clock_gettime(CLOCK_MONOTONIC_RAW, &time);
	return (uint64_t)time.tv_sec * 1000000000 + time.tv_nsec;
}

static int sample(FILE *log, int fd, void *page, size_t size,
		  uint64_t trial, int event)
{
	uint64_t start = now_ns();

	if ((event || CONSTANT_ACCESS) && pread(fd, page, size, 0) != (ssize_t)size) {
		perror("pread");
		return 1;
	}
	if (event)
		sink ^= *(unsigned char *)page;

	uint64_t duration = now_ns() - start;
	fprintf(log, "%" PRIu64 ",%" PRIu64 ",0,%d,%" PRIu64 ",%s\n",
		trial, start, event, duration,
		CONSTANT_ACCESS ? "constant" : "normal");
	fflush(log);
	printf("trial=%" PRIu64 " event=%d duration_ns=%" PRIu64 "\n",
	       trial, event, duration);
	return 0;
}

static void usage(const char *name)
{
	fprintf(stderr, "usage: %s run TARGET CSV TRIAL EVENT\n"
			"       %s interactive TARGET CSV\n", name, name);
}

int main(int argc, char **argv)
{
	int run = argc == 6 && !strcmp(argv[1], "run");
	int interactive = argc == 4 && !strcmp(argv[1], "interactive");
	if (!run && !interactive) {
		usage(argv[0]);
		return 2;
	}

	size_t size = (size_t)sysconf(_SC_PAGESIZE);
	int fd = open(argv[2], O_RDONLY);
	int log_fd = open(argv[3], O_WRONLY | O_CREAT | O_APPEND, 0666);
	if (fd == -1 || log_fd == -1) {
		perror("open");
		return 1;
	}

	struct stat status;
	fstat(log_fd, &status);
	FILE *log = fdopen(log_fd, "a");
	void *page = malloc(size);
	if (!log || !page)
		return 1;
	if (status.st_size == 0)
		fprintf(log, "trial_id,timestamp_ns,target_page,event,duration_ns,"
			"access_policy\n");

	int result = 0;
	if (run) {
		int event = atoi(argv[5]);
		result = sample(log, fd, page, size,
				strtoull(argv[4], NULL, 10), event);
	} else {
		char line[8];
		uint64_t trial = 1;
		while (printf("event [0/1/q]> "), fflush(stdout),
		       fgets(line, sizeof(line), stdin) && line[0] != 'q') {
			if ((line[0] != '0' && line[0] != '1') ||
			    sample(log, fd, page, size, trial++, line[0] - '0'))
				result = 1;
		}
	}

	free(page);
	fclose(log);
	close(fd);
	return result;
}
