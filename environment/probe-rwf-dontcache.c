#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>

#ifndef RWF_DONTCACHE
#define RWF_DONTCACHE 0x00000080
#endif

static int cached(int fd)
{
	long page_size = sysconf(_SC_PAGESIZE);
	unsigned char vec = 0;
	void *map = mmap(NULL, page_size, PROT_NONE, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED)
		return -1;
	if (mincore(map, page_size, &vec) == -1)
		vec = 0xff;
	munmap(map, page_size);
	return vec == 0xff ? -1 : !!(vec & 1);
}

static ssize_t probe(int fd)
{
	char byte;
	struct iovec iov = {.iov_base = &byte, .iov_len = 1};
	return preadv2(fd, &iov, 1, 0, RWF_NOWAIT | RWF_DONTCACHE);
}

static void report_probe(const char *label, int fd)
{
	errno = 0;
	ssize_t ret = probe(fd);
	printf("%s ret=%zd errno=%s cache=%d\n", label, ret,
	       ret < 0 ? strerror(errno) : "none", cached(fd));
}

int main(int argc, char **argv)
{
	char byte;
	if (argc != 2) {
		fprintf(stderr, "usage: %s FILE\n", argv[0]);
		return 2;
	}
	int fd = open(argv[1], O_RDONLY);
	if (fd == -1) {
		perror("open");
		return 1;
	}

	posix_fadvise(fd, 0, 4096, POSIX_FADV_DONTNEED);
	printf("uncached-before cache=%d\n", cached(fd));
	report_probe("miss-probe-1", fd);
	usleep(50000);
	printf("miss-after-50ms cache=%d\n", cached(fd));
	report_probe("miss-probe-2", fd);
	usleep(50000);
	printf("miss-after-cleanup cache=%d\n", cached(fd));

	pread(fd, &byte, 1, 0);
	printf("cached-before cache=%d\n", cached(fd));
	report_probe("cached-probe", fd);
	printf("cached-after cache=%d\n", cached(fd));
	usleep(50000);
	printf("cached-after-50ms cache=%d\n", cached(fd));
	close(fd);
	return 0;
}
