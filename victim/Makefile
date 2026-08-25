CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
DOCKER ?= docker
STATIC_BUILDER_IMAGE ?= flush-monitor-victim-builder:local

all: victim victim-constant target.bin

static: victim-static victim-constant-static target.bin

static-container: target.bin
	$(DOCKER) build -t $(STATIC_BUILDER_IMAGE) .
	$(DOCKER) run --rm --security-opt label=disable \
		--user "$(shell id -u):$(shell id -g)" \
		--mount "type=bind,src=$(CURDIR),dst=/src" \
		--workdir /src $(STATIC_BUILDER_IMAGE) \
		sh -c 'cc $(CFLAGS) -static victim.c -o victim-static && \
			cc $(CFLAGS) -static -DCONSTANT_ACCESS=1 victim.c \
			-o victim-constant-static'

victim: victim.c
	$(CC) $(CFLAGS) $< -o $@

victim-constant: victim.c
	$(CC) $(CFLAGS) -DCONSTANT_ACCESS=1 $< -o $@

victim-static: victim.c
	$(CC) $(CFLAGS) -static $< -o $@

victim-constant-static: victim.c
	$(CC) $(CFLAGS) -static -DCONSTANT_ACCESS=1 $< -o $@

clean:
	rm -f victim victim-constant victim-static victim-constant-static \
		ground-truth.csv

.PHONY: all static static-container clean
