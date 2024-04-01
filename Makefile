.PHONY: all .clangd shit clean

all: shit test

.clangd:
	rm .clangd
	echo "CompileFlags: Add: -I$(PWD)/include" > .clangd

shit:
	@echo "Creating shit..."
	$(MAKE) -C src

clean:
	@echo "Cleaning up..."
	$(MAKE) -C src clean

