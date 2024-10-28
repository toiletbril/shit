DIFF_FLAGS := -u

ifeq ($(OS), Windows_NT)
BIN := ../shit.exe
DIFF_FLAGS += -w # CRLF on Windows SMH
else
BIN := ../shit
endif

BIN_FLAGS := -AEM

FAILED_LIST := failed.diff
INPUT_FILES := $(wildcard input/*.shit)

TEST_NAMES := $(INPUT_FILES:input/%.shit=%)

# TODO: Custom test runner?
%: expected/%.out
	cat input/$@.shit | \
	$(BIN) $(BIN_FLAGS) - 2>&1 | \
	diff $(DIFF_FLAGS) $< - >> $(FAILED_LIST) && \
	printf "	%-42s ok\n" $@ || \
	printf "	%-42s FAILED :c\n" $@

clean:
	@rm -f $(FAILED_LIST)

test: $(BIN)
	@$(MAKE) -B clean
	@$(MAKE) -B run_tests
	cat $(FAILED_LIST)

run_tests: $(TEST_NAMES)

refill:
	for f in $(TEST_NAMES); do \
		printf "	%-42s $$f.out\n" $$f.shit; \
		$(BIN) $(BIN_FLAGS) input/$$f.shit > expected/$$f.out 2>&1; \
	done || true

.PHONY: test clean run_tests refill
