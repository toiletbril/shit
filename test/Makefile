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
INPUT_FILES := $(wildcard shit/*.shit)

TEST_NAMES := $(INPUT_FILES:shit/%.shit=%)

# TODO: Custom test runner?
# A test passes when its output matches the primary expected file or, for a test
# whose output is not fully deterministic such as the ordering of two background
# jobs, an alternative expected/NAME_1.out. The output is captured once to a
# temporary so the byte-for-byte trailing-newline comparison is preserved, then
# diffed against each accepted file in turn. Only the primary diff is recorded on
# a real failure.
%: expected/%.out
	@out=$$(mktemp); \
	cat shit/$@.shit | $(BIN) $(BIN_FLAGS) - > $$out 2>&1; \
	if diff $(DIFF_FLAGS) $< $$out >/dev/null 2>&1 || \
	   { [ -f expected/$@_1.out ] && \
	     diff $(DIFF_FLAGS) expected/$@_1.out $$out >/dev/null 2>&1; }; then \
		printf "	%-64s ok\n" $@; \
	else \
		diff $(DIFF_FLAGS) $< $$out >> $(FAILED_LIST); \
		printf "	%-64s FAILED :c\n" $@; \
	fi; \
	rm -f $$out

clean:
	@rm -f $(FAILED_LIST) $(SHIT_HISTORY) $(SHIT_DIRECTORY_HISTORY)

test: $(BIN)
	@$(MAKE) -B clean
	@$(MAKE) -B run_tests
	@$(MAKE) -B dashdiff
	@$(MAKE) -B bashdiff
	@$(MAKE) -B mimicrydiff
	@cat $(FAILED_LIST)
	@test ! -s $(FAILED_LIST)

run_tests: $(TEST_NAMES)

refill:
	for f in $(TEST_NAMES); do \
		printf "	%-64s $$f.out\n" $$f.shit; \
		cat shit/$$f.shit | $(BIN) $(BIN_FLAGS) - > expected/$$f.out 2>&1; \
	done || true

# Run each posix script through the shell and through dash, then diff their
# standard output through process substitution, no temporary files. A difference
# is appended to the failed list and the line is marked, the same shape the
# golden run_tests target uses. The goal is byte-identical output with dash.
DASH ?= dash
SH_COMPAT := $(wildcard sh/*.sh)

# The diff targets compare two live shells through process substitution, so they
# run in bash rather than the system /bin/sh. The scope is these two targets so
# the other recipes keep their /bin/sh semantics.
dashdiff bashdiff mimicrydiff: SHELL := bash

dashdiff: $(BIN)
	@for f in $(SH_COMPAT); do \
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
BASH_COMPAT := $(wildcard bash/*.bash)

bashdiff: $(BIN)
	@for f in $(BASH_COMPAT); do \
		s="$$($(BIN) --bash-compatible "$$f" 2>/dev/null; printf X)"; s="$${s%X}"; \
		b="$$($(BASH) "$$f" 2>/dev/null; printf X)"; b="$${b%X}"; \
		diff $(DIFF_FLAGS) --label "$$f (shit)" --label "$$f (bash)" \
			<(printf '%s' "$$s") <(printf '%s' "$$b") >> $(FAILED_LIST) \
			&& printf "	%-64s ok\n" "$$f" \
			|| printf "	%-64s FAILED :c\n" "$$f"; \
	done

# Push every bash and sh diff script through mimicry as well, so each one runs a
# second time and the shell picks its mood from the script's own shebang rather
# than an explicit flag. The script is run as a command with -I -c, which is what
# triggers mimicry, and the reference shell runs the same path so $0 and the
# positional parameters stay identical. A bash script compares against bash, an
# sh script against dash, since the POSIX mood targets dash semantics. stderr is
# dropped, so a shifted diagnostic line number does not affect the comparison.
mimicrydiff: $(BIN)
	@for f in $(BASH_COMPAT); do \
		s="$$($(BIN) -I -c "$$f" 2>/dev/null; printf X)"; s="$${s%X}"; \
		b="$$($(BASH) "$$f" 2>/dev/null; printf X)"; b="$${b%X}"; \
		diff $(DIFF_FLAGS) --label "$$f (shit -I)" --label "$$f (bash)" \
			<(printf '%s' "$$s") <(printf '%s' "$$b") >> $(FAILED_LIST) \
			&& printf "	%-64s mimic ok\n" "$$f" \
			|| printf "	%-64s mimic FAILED :c\n" "$$f"; \
	done; \
	for f in $(SH_COMPAT); do \
		s="$$($(BIN) -I -c "$$f" 2>/dev/null; printf X)"; s="$${s%X}"; \
		d="$$($(DASH) "$$f" 2>/dev/null; printf X)"; d="$${d%X}"; \
		diff $(DIFF_FLAGS) --label "$$f (shit -I)" --label "$$f (dash)" \
			<(printf '%s' "$$s") <(printf '%s' "$$d") >> $(FAILED_LIST) \
			&& printf "	%-64s mimic ok\n" "$$f" \
			|| printf "	%-64s mimic FAILED :c\n" "$$f"; \
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

.PHONY: test clean run_tests refill dashdiff bashdiff mimicrydiff bench
