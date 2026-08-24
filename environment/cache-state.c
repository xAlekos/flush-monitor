#define _GNU_SOURCE

#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s FILE\n", argv[0]);
		return 2;
	}
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
	unsigned char vec = 0;
	if (mincore(map, page_size, &vec) == -1) {
		perror("mincore");
		return 1;
	}
	printf("validator_uid=%ld cache=%d\n", (long)geteuid(), !!(vec & 1));
	return 0;
}
