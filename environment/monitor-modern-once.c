#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

#ifndef RWF_DONTCACHE
#define RWF_DONTCACHE 0x00000080
#endif

static ssize_t probe(int fd, int flags)
{
	char byte;
	struct iovec iov = {.iov_base = &byte, .iov_len = 1};
	return preadv2(fd, &iov, 1, 0, flags);
}

static int flush_page(int fd)
{
	int ret = posix_fadvise(fd, 0, 4096, POSIX_FADV_DONTNEED);
	if (ret)
		errno = ret;
	return ret ? -1 : 0;
}

int main(int argc, char **argv)
{
	if (argc != 3 || (strcmp(argv[1], "flush") &&
			 strcmp(argv[1], "dontcache"))) {
		fprintf(stderr, "usage: %s flush|dontcache FILE\n", argv[0]);
		return 2;
	}
	int fd = open(argv[2], O_RDONLY);
	if (fd == -1) {
		perror("open");
		return 1;
	}
	int dontcache = !strcmp(argv[1], "dontcache");
	int flags = RWF_NOWAIT | (dontcache ? RWF_DONTCACHE : 0);
	errno = 0;
	ssize_t ret = probe(fd, flags);
	if (ret == 1) {
		if (flush_page(fd) == -1) {
			perror("flush hit");
			return 1;
		}
		printf("uid=%ld mode=%s classification=hit cleanup=flush\n",
		       (long)geteuid(), argv[1]);
		return 0;
	}
	if (ret != -1 || errno != EAGAIN) {
		fprintf(stderr, "initial probe: ret=%zd errno=%s\n", ret,
			strerror(errno));
		return 1;
	}

	unsigned long polls;
	for (polls = 1; polls <= 10000000; ++polls) {
		errno = 0;
		ret = probe(fd, flags);
		if (ret == 1)
			break;
		if (ret != -1 || errno != EAGAIN) {
			fprintf(stderr, "cleanup probe: ret=%zd errno=%s\n", ret,
				strerror(errno));
			return 1;
		}
	}
	if (polls > 10000000) {
		fprintf(stderr, "cleanup timed out\n");
		return 1;
	}
	if (!dontcache && flush_page(fd) == -1) {
		perror("flush miss");
		return 1;
	}
	printf("uid=%ld mode=%s classification=miss cleanup=%s polls=%lu\n",
	       (long)geteuid(), argv[1], dontcache ? "dontcache" : "flush",
	       polls);
	return 0;
}
