DFLAGS := -u

ifeq ($(OS), Windows_NT)
BIN := ../shit.exe
DFLAGS := -w # CRLF on Windows SMH
else
BIN := ../shit
endif

FAILED_LIST := failed.diff

# TODO: Custom test runner?
%: expected/%.out
	cat input/$@.shit | \
	$(BIN) -AE - 2>&1 | \
	diff $(DFLAGS) $< - >> $(FAILED_LIST) && \
	printf "	[%32s] ok.\n" $@ || \
	printf "	[%32s] FAILED.\n" $@

clean:
	@rm -f $(FAILED_LIST)

test: $(BIN)
	@$(MAKE) -B clean
	@$(MAKE) -B run_tests
	cat $(FAILED_LIST)

# NOTE: Input shouldn't have newline at the end of file.
INPUT_FILES := $(wildcard input/*.shit)

run_tests: $(INPUT_FILES:input/%.shit=%)

.PHONY: test clean run_tests
