ifndef VERBOSE
MAKEFLAGS += -s
endif

all: shit test

shit:
	echo "Creating shit..."
	$(MAKE) -C src shit

fmt:
	$(MAKE) -C src fmt

test: shit
	echo "Launching tests..."
	$(MAKE) -C test test

clean:
	echo "Cleaning up..."
	$(MAKE) -C src clean
	$(MAKE) -C test clean

.PHONY: all shit fmt test clean
