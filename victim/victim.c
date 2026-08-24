#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static uint64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static int record(FILE *log, int fd, void *buf, size_t page_size,
		  uint64_t trial, int event)
{
	uint64_t timestamp = now_ns();
	if (event && pread(fd, buf, page_size, 0) != (ssize_t)page_size) {
		perror("pread");
		return -1;
	}
	fprintf(log, "%" PRIu64 ",%" PRIu64 ",0,%d\n", trial, timestamp,
		event);
	fflush(log);
	printf("trial=%" PRIu64 " timestamp_ns=%" PRIu64 " event=%d\n",
	       trial, timestamp, event);
	return 0;
}

static int parse_event(const char *text, int *event)
{
	if (!strcmp(text, "0") || !strcmp(text, "1")) {
		*event = text[0] - '0';
		return 0;
	}
	return -1;
}

static void usage(const char *name)
{
	fprintf(stderr,
		"usage: %s interactive TARGET CSV\n"
		"       %s run TARGET CSV TRIAL_ID EVENT\n",
		name, name);
}

int main(int argc, char **argv)
{
	int interactive = argc == 4 && !strcmp(argv[1], "interactive");
	int automated = argc == 6 && !strcmp(argv[1], "run");
	if (!interactive && !automated) {
		usage(argv[0]);
		return 2;
	}

	long page_size = sysconf(_SC_PAGESIZE);
	int fd = open(argv[2], O_RDONLY);
	if (fd == -1) {
		perror("open target");
		return 1;
	}
	struct stat target;
	if (fstat(fd, &target) == -1 || target.st_size < page_size) {
		fprintf(stderr, "target must contain at least one page\n");
		return 1;
	}

	int log_fd = open(argv[3], O_WRONLY | O_CREAT | O_APPEND, 0600);
	if (log_fd == -1) {
		perror("open ground truth");
		return 1;
	}
	struct stat log_stat;
	if (fstat(log_fd, &log_stat) == -1)
		return 1;
	FILE *log = fdopen(log_fd, "a");
	if (!log)
		return 1;
	if (log_stat.st_size == 0)
		fprintf(log, "trial_id,timestamp_ns,target_page,event\n");

	void *buf = malloc(page_size);
	if (!buf)
		return 1;

	int status = 0;
	if (automated) {
		char *end;
		errno = 0;
		uint64_t trial = strtoull(argv[4], &end, 10);
		int event;
		if (errno || *end || parse_event(argv[5], &event) == -1) {
			usage(argv[0]);
			status = 2;
		} else if (record(log, fd, buf, page_size, trial, event) == -1) {
			status = 1;
		}
	} else {
		char line[32];
		uint64_t trial = 1;
		while (printf("event [0/1/q]> "), fflush(stdout),
		       fgets(line, sizeof(line), stdin)) {
			if (line[0] == 'q')
				break;
			int event;
			line[strcspn(line, "\n")] = 0;
			if (parse_event(line, &event) == -1) {
				fprintf(stderr, "enter 0, 1, or q\n");
				continue;
			}
			if (record(log, fd, buf, page_size, trial++, event) == -1) {
				status = 1;
				break;
			}
		}
	}

	free(buf);
	fclose(log);
	close(fd);
	return status;
}
