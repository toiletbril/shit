DIFF_FLAGS := -u

ifeq ($(OS), Windows_NT)
BIN ?= ../shit.exe
DIFF_FLAGS += -w # CRLF on Windows SMH
else
BIN ?= ../shit
endif

BIN_FLAGS := -AEM

# The suite runs cd and z, which write the directory history, so both the
# command history and the directory history are redirected to ignored in-repo
# files. Running the suite then never reads or corrupts the developer's global
# ~/.shit_history or ~/.shit_directory_history.
export SHIT_HISTORY := $(CURDIR)/.test_history
export SHIT_DIRECTORY_HISTORY := $(CURDIR)/.test_directory_history

FAILED_LIST := failed.diff
INPUT_FILES := $(wildcard input/*.shit)

TEST_NAMES := $(INPUT_FILES:input/%.shit=%)

# TODO: Custom test runner?
%: expected/%.out
	cat input/$@.shit | \
	$(BIN) $(BIN_FLAGS) - 2>&1 | \
	diff $(DIFF_FLAGS) $< - >> $(FAILED_LIST) && \
	printf "	%-64s ok\n" $@ || \
	printf "	%-64s FAILED :c\n" $@

clean:
	@rm -f $(FAILED_LIST) $(SHIT_HISTORY) $(SHIT_DIRECTORY_HISTORY)

test: $(BIN)
	@$(MAKE) -B clean
	@$(MAKE) -B run_tests
	@$(MAKE) -B dashdiff
	@$(MAKE) -B bashdiff
	cat $(FAILED_LIST)

run_tests: $(TEST_NAMES)

refill:
	for f in $(TEST_NAMES); do \
		printf "	%-64s $$f.out\n" $$f.shit; \
		cat input/$$f.shit | $(BIN) $(BIN_FLAGS) - > expected/$$f.out 2>&1; \
	done || true

# Run each posix script through the shell and through dash, then diff their
# standard output through process substitution, no temporary files. A difference
# is appended to the failed list and the line is marked, the same shape the
# golden run_tests target uses. The goal is byte-identical output with dash.
DASH ?= dash
POSIX_COMPAT := $(wildcard posix/*.shit)

# The diff targets compare two live shells through process substitution, so they
# run in bash rather than the system /bin/sh. The scope is these two targets so
# the other recipes keep their /bin/sh semantics.
dashdiff bashdiff: SHELL := bash

dashdiff: $(BIN)
	@for f in $(POSIX_COMPAT); do \
		s="$$($(BIN) "$$f" 2>/dev/null; printf X)"; s="$${s%X}"; \
		d="$$($(DASH) "$$f" 2>/dev/null; printf X)"; d="$${d%X}"; \
		diff $(DIFF_FLAGS) --label "$$f (shit)" --label "$$f (dash)" \
			<(printf '%s' "$$s") <(printf '%s' "$$d") >> $(FAILED_LIST) \
			&& printf "	%-64s ok\n" "$$f" \
			|| printf "	%-64s FAILED :c\n" "$$f"; \
	done

# Run each bash script through the shell in bash mode and through bash, then diff
# their standard output through process substitution, no temporary files. A
# difference is appended to the failed list. The goal is byte-identical output
# with real bash for the bash extensions.
BASH ?= bash
BASH_COMPAT := $(wildcard bash/*.shit)

bashdiff: $(BIN)
	@for f in $(BASH_COMPAT); do \
		s="$$($(BIN) --bash-compatible "$$f" 2>/dev/null; printf X)"; s="$${s%X}"; \
		b="$$($(BASH) "$$f" 2>/dev/null; printf X)"; b="$${b%X}"; \
		diff $(DIFF_FLAGS) --label "$$f (shit)" --label "$$f (bash)" \
			<(printf '%s' "$$s") <(printf '%s' "$$b") >> $(FAILED_LIST) \
			&& printf "	%-64s ok\n" "$$f" \
			|| printf "	%-64s FAILED :c\n" "$$f"; \
	done

# Time the builtin-only configure script through each shell, after checking that
# the output is identical. Use the optimized shit build, MODE=rel, for a
# fair comparison. SCALE scales the work, the default keeps dash near 0.1s.
ZSH ?= zsh
GOSH ?= gosh

BENCH := bench/configure.shit
SCALE ?= 100

bench:
	@SCALE=$(SCALE) $(DASH) $(BENCH) >/tmp/shit_bench_d 2>&1; \
	SCALE=$(SCALE) $(BIN) --no-diagnostics $(BENCH) >/tmp/shit_bench_s 2>&1; \
	if cmp -s /tmp/shit_bench_d /tmp/shit_bench_s; then \
		echo "output matches dash"; \
	else \
		echo "output differs from dash:"; \
		diff /tmp/shit_bench_d /tmp/shit_bench_s | head -20; \
	fi; \
	echo "wall-clock seconds at SCALE=$(SCALE), lower is better:"; \
	$(BASH) -c 'export TIMEFORMAT="  %R"; \
		for sh in "$(DASH)" "$(BASH)" "$(ZSH)" "$(GOSH)" "$(BIN)"; do \
			printf "  %-10s" "$$sh"; \
			flags=; [ "$$sh" = "$(BIN)" ] && flags=--no-diagnostics; \
			( time SCALE=$(SCALE) $$sh $$flags $(BENCH) >/dev/null 2>&1 ) 2>&1; \
		done'; \
	rm -f /tmp/shit_bench_d /tmp/shit_bench_s

.PHONY: test clean run_tests refill dashdiff bashdiff bench
