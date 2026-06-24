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

BIN_FLAGS := -AER

export SHIT_HISTORY := $(CURDIR)/.test_history
export SHIT_DIRECTORY_HISTORY := $(CURDIR)/.test_directory_history

FAILED_LIST := failed.diff
INPUT_FILES := $(wildcard shit/*.shit)

SHELL := /bin/bash


TEST_NAMES := $(INPUT_FILES:shit/%.shit=%)

%: expected/%.out
	@BIN='$(BIN)' BIN_FLAGS='$(BIN_FLAGS)' DIFF_FLAGS='$(DIFF_FLAGS)' \
		FAILED_LIST='$(FAILED_LIST)' /bin/bash shittests.sh $*

clean:
	@rm -f $(FAILED_LIST) $(SHIT_HISTORY) $(SHIT_DIRECTORY_HISTORY)

test: $(BIN)
	@$(MAKE) -B clean
	@$(MAKE) -B shit_tests
	@$(MAKE) -B cli_tests
	@$(MAKE) -B completion_tests
	@$(MAKE) -B dashdiff
	@$(MAKE) -B bashdiff
	@$(MAKE) -B mimicrydiff
	@touch $(FAILED_LIST)
	@cat $(FAILED_LIST)
	@test ! -s $(FAILED_LIST)

shit_tests: $(BIN)
	@BIN='$(BIN)' BIN_FLAGS='$(BIN_FLAGS)' DIFF_FLAGS='$(DIFF_FLAGS)' \
		FAILED_LIST='$(FAILED_LIST)' /bin/bash shittests.sh $(TEST_NAMES)


CLI_INPUT := $(wildcard cli/*.sh)
CLI_NAMES := $(CLI_INPUT:cli/%.sh=cli_%)

cli_%: expected/cli/%.out
	@BIN='$(BIN)' DIFF_FLAGS='$(DIFF_FLAGS)' FAILED_LIST='$(FAILED_LIST)' \
		/bin/bash clitests.sh cli/$*.sh

cli_tests: $(BIN)
	@BIN='$(BIN)' DIFF_FLAGS='$(DIFF_FLAGS)' FAILED_LIST='$(FAILED_LIST)' \
		/bin/bash clitests.sh $(CLI_INPUT)


COMPLETION_INPUT := $(wildcard completion/*.sh)
COMPLETION_NAMES := $(COMPLETION_INPUT:completion/%.sh=completion_%)

completion_%: expected/completion/%.out
	@if ! $(BIN) --debug-complete-at '' </dev/null >/dev/null 2>&1; then \
		printf "	%-64s skipped, release binary\n" completion/$*.sh; \
	else \
		out=$$(mktemp); \
		BIN=$(BIN) sh completion/$*.sh > $$out 2>/dev/null; \
		if diff $(DIFF_FLAGS) $< $$out >/dev/null 2>&1; then \
			printf "	%-64s ok\r" completion/$*.sh; \
		else \
			diff $(DIFF_FLAGS) $< $$out >> $(FAILED_LIST); \
			printf "	%-64s FAILED :c\n" completion/$*.sh; \
		fi; \
		rm -f $$out; \
	fi

completion_tests: $(COMPLETION_NAMES)


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


ZSH ?= /bin/zsh
ASH ?= /bin/busybox ash
YASH ?= /bin/yash
BASH ?= /bin/bash
DASH ?= /bin/dash

SH_COMPAT := $(filter-out %_1.sh,$(wildcard sh/*.sh))

dashdiff: $(BIN)
	@BIN='$(BIN)' DASH='$(DASH)' DIFF_FLAGS='$(DIFF_FLAGS)' \
		FAILED_LIST='$(FAILED_LIST)' SH_COMPAT='$(SH_COMPAT)' \
		/bin/bash dashdiff.sh


BASH_COMPAT := $(filter-out %_1.bash,$(wildcard bash/*.bash))

bashdiff: $(BIN)
	@BIN='$(BIN)' BASH='$(BASH)' DIFF_FLAGS='$(DIFF_FLAGS)' \
		FAILED_LIST='$(FAILED_LIST)' BASH_COMPAT_FILES='$(BASH_COMPAT)' \
		/bin/bash bashdiff.sh

mimicrydiff: $(BIN)
	@BIN='$(BIN)' BASH='$(BASH)' DASH='$(DASH)' DIFF_FLAGS='$(DIFF_FLAGS)' \
		FAILED_LIST='$(FAILED_LIST)' SH_COMPAT='$(SH_COMPAT)' \
		BASH_COMPAT_FILES='$(BASH_COMPAT)' /bin/bash mimicrydiff.sh


BENCH := bench/configure.sh
BENCH_BASH := bench/configure.bash
BENCH_SHIT := bench/configure.shit
PRIMES := bench/primes.bash
PRIMES_PY := bench/primes.py
PRIMES_LIMIT ?= 100000
SCALE ?= 100

bench: BIN := ../shit
bench:
	@SCALE='$(SCALE)' BIN='$(BIN)' DASH='$(DASH)' BASH='$(BASH)' ZSH='$(ZSH)' \
		ASH='$(ASH)' YASH='$(YASH)' BENCH='$(BENCH)' BENCH_BASH='$(BENCH_BASH)' \
		BENCH_SHIT='$(BENCH_SHIT)' PRIMES='$(PRIMES)' PRIMES_PY='$(PRIMES_PY)' \
		PRIMES_LIMIT='$(PRIMES_LIMIT)' /bin/bash bench.sh

.PHONY: test clean shit_tests refill dashdiff bashdiff mimicrydiff bench \
		completion_tests completion_refill cli_tests
