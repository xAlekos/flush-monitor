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
	struct iovec iov = {&byte, 1};
	return preadv2(fd, &iov, 1, 0, flags);
}

static int flush(int fd)
{
	int error = posix_fadvise(fd, 0, 4096, POSIX_FADV_DONTNEED);
	if (error)
		errno = error;
	return error;
}

int main(int argc, char **argv)
{
	if (argc != 3 || (strcmp(argv[1], "flush") && strcmp(argv[1], "dontcache"))) {
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
	ssize_t result = probe(fd, flags);

	if (result == 1) {
		if (flush(fd)) {
			perror("flush");
			return 1;
		}
		puts("classification=hit");
		return 0;
	}
	if (result != -1 || errno != EAGAIN) {
		perror("preadv2");
		return 1;
	}

	/* Since Linux 6.12, a NOWAIT miss may start asynchronous readahead. */
	do {
		result = probe(fd, flags);
	} while (result == -1 && errno == EAGAIN);
	if (result != 1 || (!dontcache && flush(fd))) {
		perror("cleanup");
		return 1;
	}
	puts("classification=miss");
	return 0;
}
