ifndef VERBOSE
MAKEFLAGS += -s
endif

MAKE := $(MAKE) -j$(shell nproc)

MODE ?= dbg

ifeq ($(OS), Windows_NT)
TARGET ?= Windows_NT
else
TARGET ?= $(shell uname -s)
endif

export MODE
export TARGET

all: shit test

shit:
	echo Creating shit...
	$(MAKE) -C src shit

install:
	echo Installing...
	$(MAKE) -C src install

uninstall:
	echo Uninstalling...
	$(MAKE) -C src uninstall

tidy:
	echo Launching '$$'CLANG_TIDY...
	$(MAKE) -C src tidy

fmt:
	echo Launching '$$'CLANG_FMT...
	$(MAKE) -C src fmt

test: shit
	echo Launching tests...
	$(MAKE) -C test test

refill_tests: shit
	echo Refilling tests...
	$(MAKE) -C test refill

clean:
	echo Cleaning up...
	$(MAKE) -C src clean
	$(MAKE) -C test clean

.PHONY: all shit install uninstall tidy fmt test refill_tests clean
