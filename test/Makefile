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

# Run each everything_*.shit through the shell and through dash, then compare
# their standard output and exit status. The goal is byte-identical behavior.
DASH ?= dash
EVERYTHING := $(wildcard everything/*.shit)

dashdiff: $(BIN)
	@for f in $(EVERYTHING); do \
		$(BIN) "$$f" >/tmp/shit_dd_s 2>/dev/null; shit_rc=$$?; \
		$(DASH) "$$f" >/tmp/shit_dd_d 2>/dev/null; dash_rc=$$?; \
		if cmp -s /tmp/shit_dd_s /tmp/shit_dd_d && [ "$$shit_rc" = "$$dash_rc" ]; then \
			printf "	%-42s ok\n" "$$f"; \
		else \
			printf "	%-42s DIFF (shit rc=%s dash rc=%s)\n" "$$f" "$$shit_rc" "$$dash_rc"; \
			diff /tmp/shit_dd_d /tmp/shit_dd_s | head -20; \
		fi; \
	done; \
	rm -f /tmp/shit_dd_s /tmp/shit_dd_d

.PHONY: test clean run_tests refill dashdiff
