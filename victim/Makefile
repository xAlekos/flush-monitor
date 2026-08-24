CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror
PAGE_SIZE := $(shell getconf PAGESIZE)

all: victim target.bin

static: victim-static target.bin

victim: victim.c
	$(CC) $(CFLAGS) $< -o $@

victim-static: victim.c
	$(CC) $(CFLAGS) -static $< -o $@

target.bin:
	dd if=/dev/urandom of=$@ bs=$(PAGE_SIZE) count=1 conv=fsync status=none

clean:
	rm -f victim victim-static target.bin ground-truth.csv

.PHONY: all static clean
