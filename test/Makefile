DIFF_FLAGS := -u
MODE ?= dbg
ifeq ($(OS), Windows_NT)
TARGET ?= Windows_NT
else
TARGET ?= $(shell uname -s)
endif

MODE_SUFFIX := $(if $(filter rel,$(MODE)),,-$(MODE))
ifeq ($(MODE), cosmo)
BIN ?= ../shit$(MODE_SUFFIX).com
else ifeq ($(TARGET), Windows_NT)
BIN ?= ../shit$(MODE_SUFFIX).exe
DIFF_FLAGS += -w # CRLF on Windows SMH
else
BIN ?= ../shit$(MODE_SUFFIX)
endif

# The test drivers run with the current directory set to test, so a BIN given
# relative to the repo root or as a bare name is resolved to an absolute path
# here. A path that exists as given wins, then the repo-root sibling, then a
# PATH lookup, so the target binary is found no matter where make was launched.
override BIN := $(shell \
	if [ -x "$(BIN)" ]; then \
		echo "$$(cd "$$(dirname "$(BIN)")" && pwd)/$$(basename "$(BIN)")"; \
	elif [ -x "$(CURDIR)/../$(BIN)" ]; then \
		candidate="$(CURDIR)/../$(BIN)"; \
		echo "$$(cd "$$(dirname "$$candidate")" && pwd)/$$(basename "$$candidate")"; \
	elif command -v "$(BIN)" >/dev/null 2>&1; then \
		found=$$(command -v "$(BIN)"); \
		echo "$$(cd "$$(dirname "$$found")" && pwd)/$$(basename "$$found")"; \
	else echo "$(BIN)"; fi)

BIN_FLAGS := -AER

export SHIT_HISTORY := $(CURDIR)/.test_history
export SHIT_DIRECTORY_HISTORY := $(CURDIR)/.test_directory_history

FAILED_LIST := failed.diff
INPUT_FILES := $(wildcard shit/*.shit)

SHELL := /bin/bash


TEST_NAMES := $(INPUT_FILES:shit/%.shit=%)

%: expected/%.out
	@BIN='$(BIN)' BIN_FLAGS='$(BIN_FLAGS)' DIFF_FLAGS='$(DIFF_FLAGS)' \
		FAILED_LIST='$(FAILED_LIST)' $(SHELL) run-shit-test.sh $*

clean:
	@rm -f $(FAILED_LIST) $(SHIT_HISTORY) $(SHIT_DIRECTORY_HISTORY)

TEST_SUITES := shit_tests cli_tests completion_tests highlight_tests \
		dashdiff bashdiff mimicrydiff

test: $(BIN)
	@$(MAKE) clean
	@$(MAKE) $(TEST_SUITES)
	@touch $(FAILED_LIST)
	@cat $(FAILED_LIST)
	@test ! -s $(FAILED_LIST)

shit_tests: $(BIN) $(TEST_NAMES)


CLI_INPUT := $(wildcard cli/*.sh)
CLI_NAMES := $(CLI_INPUT:cli/%.sh=cli_%)

cli_%: expected/cli/%.out
	@BIN='$(BIN)' DIFF_FLAGS='$(DIFF_FLAGS)' FAILED_LIST='$(FAILED_LIST)' \
		$(SHELL) run-cli-test.sh cli/$*.sh

cli_tests: $(BIN) $(CLI_NAMES)


COMPLETION_INPUT := $(wildcard completion/*.sh)
COMPLETION_NAMES := $(COMPLETION_INPUT:completion/%.sh=completion_%)

completion_%: expected/completion/%.out
	@if ! $(BIN) --debug-complete-at '' </dev/null >/dev/null 2>&1; then \
		printf "	%-64s skipped, release binary\n" completion/$*.sh; \
	else \
		out=$$(mktemp); \
		BIN=$(BIN) sh completion/$*.sh > $$out 2>/dev/null; \
		if diff $(DIFF_FLAGS) $< $$out >/dev/null 2>&1; then \
			printf "	%-64s ok\033[K\r" completion/$*.sh; \
		else \
			diff $(DIFF_FLAGS) $< $$out >> $(FAILED_LIST); \
			printf "	%-64s FAILED :c\n" completion/$*.sh; \
		fi; \
		rm -f $$out; \
	fi

completion_tests: $(BIN)
	@if ! $(BIN) --debug-complete-at '' </dev/null >/dev/null 2>&1; then \
		printf "	%-64s skipped, release binary\n" completion; \
	else \
		$(MAKE) --no-print-directory $(COMPLETION_NAMES) \
			BIN='$(BIN)' DIFF_FLAGS='$(DIFF_FLAGS)' \
			FAILED_LIST='$(FAILED_LIST)'; \
	fi


HIGHLIGHT_INPUT := $(wildcard highlight/*.sh)
HIGHLIGHT_NAMES := $(HIGHLIGHT_INPUT:highlight/%.sh=highlight_%)

highlight_%: expected/highlight/%.out
	@BIN='$(BIN)' DIFF_FLAGS='$(DIFF_FLAGS)' FAILED_LIST='$(FAILED_LIST)' \
		$(SHELL) run-highlight-test.sh highlight/$*.sh

highlight_tests: $(BIN)
	@BIN='$(BIN)' DIFF_FLAGS='$(DIFF_FLAGS)' FAILED_LIST='$(FAILED_LIST)' \
		$(SHELL) run-highlight-test.sh $(HIGHLIGHT_INPUT)


REFILL ?=

refill:
	for f in $${REFILL:-$(TEST_NAMES)}; do \
		[ -f shit/$$f.shit ] || continue; \
		printf "	%-64s $$f.out\n" $$f.shit; \
		cat shit/$$f.shit | $(BIN) $(BIN_FLAGS) - > expected/$$f.out 2>&1; \
	done || true
	for f in $${REFILL:-$(CLI_INPUT:cli/%.sh=%)}; do \
		[ -f cli/$$f.sh ] || continue; \
		printf "	%-64s cli/$$f.out\n" cli/$$f.sh; \
		BIN=$(BIN) sh cli/$$f.sh > expected/cli/$$f.out 2>&1; \
	done || true
	for f in $(COMPLETION_INPUT:completion/%.sh=%); do \
		printf "	%-64s $$f.out\n" completion/$$f.sh; \
		BIN=$(BIN) sh completion/$$f.sh > expected/completion/$$f.out 2>/dev/null; \
	done || true
	for f in $(HIGHLIGHT_INPUT:highlight/%.sh=%); do \
		printf "	%-64s $$f.out\n" highlight/$$f.sh; \
		BIN=$(BIN) sh highlight/$$f.sh > expected/highlight/$$f.out 2>/dev/null; \
	done || true


ZSH ?= /bin/zsh
ASH ?= /bin/busybox ash
YASH ?= /bin/yash
BASHP ?= /bin/bash
DASH ?= /bin/dash

SH_COMPAT := $(filter-out %_1.sh,$(wildcard sh/*.sh))

dashdiff: $(BIN)
	@BIN='$(BIN)' DASH='$(DASH)' DIFF_FLAGS='$(DIFF_FLAGS)' \
		FAILED_LIST='$(FAILED_LIST)' SH_COMPAT='$(SH_COMPAT)' \
		$(SHELL) run-dash-diff-test.sh


BASH_COMPAT := $(filter-out %_1.bash,$(wildcard bash/*.bash))

bashdiff: $(BIN)
	@BIN='$(BIN)' BASHP='$(BASHP)' DIFF_FLAGS='$(DIFF_FLAGS)' \
		FAILED_LIST='$(FAILED_LIST)' BASH_COMPAT_FILES='$(BASH_COMPAT)' \
		$(SHELL) run-bash-diff-test.sh

mimicrydiff: $(BIN)
	@BIN='$(BIN)' BASHP='$(BASHP)' DASH='$(DASH)' DIFF_FLAGS='$(DIFF_FLAGS)' \
		FAILED_LIST='$(FAILED_LIST)' SH_COMPAT='$(SH_COMPAT)' \
		BASH_COMPAT_FILES='$(BASH_COMPAT)' $(SHELL) run-mimicry-diff-test.sh


BENCH := bench/configure.sh
BENCH_BASH := bench/configure.bash
BENCH_SHIT := bench/configure.shit
PRIMES := bench/primes.bash
PRIMES_PY := bench/primes.py
PRIMES_LIMIT ?= 100000
SCALE ?= 100

bench:
	@SCALE='$(SCALE)' BIN='$(BIN)' DASH='$(DASH)' BASHP='$(BASHP)' ZSH='$(ZSH)' \
		ASH='$(ASH)' YASH='$(YASH)' BENCH='$(BENCH)' BENCH_BASH='$(BENCH_BASH)' \
		BENCH_SHIT='$(BENCH_SHIT)' PRIMES='$(PRIMES)' PRIMES_PY='$(PRIMES_PY)' \
		PRIMES_LIMIT='$(PRIMES_LIMIT)' $(SHELL) run-bench-test.sh

.PHONY: test clean shit_tests refill dashdiff bashdiff mimicrydiff bench \
		completion_tests completion_refill cli_tests highlight_tests
