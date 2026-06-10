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
SH_COMPAT := $(filter-out %_1.sh,$(wildcard sh/*.sh))

# The diff targets compare two live shells through process substitution, so they
# run in bash rather than the system /bin/sh. The scope is these two targets so
# the other recipes keep their /bin/sh semantics.
dashdiff bashdiff mimicrydiff: SHELL := bash

# A NAME_1.sh variant marks NAME.sh flaky across dash versions, the same
# accepted-alternative idea the bashdiff _1.bash files carry.
dashdiff: $(BIN)
	@for f in $(SH_COMPAT); do \
		s="$$($(BIN) "$$f" 2>/dev/null; printf X)"; s="$${s%X}"; \
		d="$$($(DASH) "$$f" 2>/dev/null; printf X)"; d="$${d%X}"; \
		alt="$${f%.sh}_1.sh"; \
		if [ "$$s" = "$$d" ]; then \
			printf "	%-64s ok\n" "$$f"; \
		elif [ -f "$$alt" ] && \
			d1="$$($(DASH) "$$alt" 2>/dev/null; printf X)" && \
			[ "$$s" = "$${d1%X}" ]; then \
			printf "	%-64s ok (flaky alternative)\n" "$$f"; \
		else \
			diff $(DIFF_FLAGS) --label "$$f (shit)" --label "$$f (dash)" \
				<(printf '%s' "$$s") <(printf '%s' "$$d") >> $(FAILED_LIST); \
			printf "	%-64s FAILED :c\n" "$$f"; \
		fi; \
	done

# Run each bash script through the shell in bash mode and through bash, then diff
# their standard output through process substitution, no temporary files. A
# difference is appended to the failed list. The goal is byte-identical output
# with real bash for the bash extensions.
BASH ?= bash
BASH_COMPAT := $(filter-out %_1.bash,$(wildcard bash/*.bash))

# A NAME_1.bash variant marks NAME.bash flaky across bash versions, the same
# accepted-alternative idea the golden _1.out files carry. The variant spells
# the other version's output, so the test passes when shit matches the local
# bash on NAME.bash or matches bash running NAME_1.bash.
bashdiff: $(BIN)
	@for f in $(BASH_COMPAT); do \
		s="$$($(BIN) --bash-compatible "$$f" 2>/dev/null; printf X)"; s="$${s%X}"; \
		b="$$($(BASH) "$$f" 2>/dev/null; printf X)"; b="$${b%X}"; \
		alt="$${f%.bash}_1.bash"; \
		if [ "$$s" = "$$b" ]; then \
			printf "	%-64s ok\n" "$$f"; \
		elif [ -f "$$alt" ] && \
			b1="$$($(BASH) "$$alt" 2>/dev/null; printf X)" && \
			[ "$$s" = "$${b1%X}" ]; then \
			printf "	%-64s ok (flaky alternative)\n" "$$f"; \
		else \
			diff $(DIFF_FLAGS) --label "$$f (shit)" --label "$$f (bash)" \
				<(printf '%s' "$$s") <(printf '%s' "$$b") >> $(FAILED_LIST); \
			printf "	%-64s FAILED :c\n" "$$f"; \
		fi; \
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
		alt="$${f%.bash}_1.bash"; \
		if [ "$$s" = "$$b" ]; then \
			printf "	%-64s mimic ok\n" "$$f"; \
		elif [ -f "$$alt" ] && \
			b1="$$($(BASH) "$$alt" 2>/dev/null; printf X)" && \
			[ "$$s" = "$${b1%X}" ]; then \
			printf "	%-64s mimic ok (flaky alternative)\n" "$$f"; \
		else \
			diff $(DIFF_FLAGS) --label "$$f (shit -I)" --label "$$f (bash)" \
				<(printf '%s' "$$s") <(printf '%s' "$$b") >> $(FAILED_LIST); \
			printf "	%-64s mimic FAILED :c\n" "$$f"; \
		fi; \
	done; \
	for f in $(SH_COMPAT); do \
		s="$$($(BIN) -I -c "$$f" 2>/dev/null; printf X)"; s="$${s%X}"; \
		d="$$($(DASH) "$$f" 2>/dev/null; printf X)"; d="$${d%X}"; \
		alt="$${f%.sh}_1.sh"; \
		if [ "$$s" = "$$d" ]; then \
			printf "	%-64s mimic ok\n" "$$f"; \
		elif [ -f "$$alt" ] && \
			d1="$$($(DASH) "$$alt" 2>/dev/null; printf X)" && \
			[ "$$s" = "$${d1%X}" ]; then \
			printf "	%-64s mimic ok (flaky alternative)\n" "$$f"; \
		else \
			diff $(DIFF_FLAGS) --label "$$f (shit -I)" --label "$$f (dash)" \
				<(printf '%s' "$$s") <(printf '%s' "$$d") >> $(FAILED_LIST); \
			printf "	%-64s mimic FAILED :c\n" "$$f"; \
		fi; \
	done

# Time the builtin-only configure script through each shell, after checking that
# the output is identical. Use the optimized shit build, MODE=rel, for a
# fair comparison. SCALE scales the work, the default keeps dash near 0.1s.
ZSH ?= zsh
GOSH ?= gosh

BENCH := bench/configure.sh
BENCH_BASH := bench/configure.bash
SCALE ?= 100

# Each shell runs exactly once. The single timed pass redirects its output to a
# per-shell temp file, so the captured wall-clock and the captured output both
# come from that one run rather than running the shell again for the comparison.
# The POSIX script is checked against dash and the bash script against bash. cmp
# runs without -s so a mismatch names the first differing byte and line, then a
# diff shows the differing lines.
bench:
	@$(BASH) -c 'export TIMEFORMAT="  %R"; scale="$(SCALE)"; \
		d=/tmp/shit_bench_d; b=/tmp/shit_bench_b; z=/tmp/shit_bench_z; \
		g=/tmp/shit_bench_g; s=/tmp/shit_bench_s; \
		bb=/tmp/shit_bench_bb; sb=/tmp/shit_bench_sb; \
		echo "configure.sh, wall-clock seconds at SCALE=$$scale, lower is better:"; \
		printf "  %-10s" "$(DASH)"; ( time SCALE=$$scale $(DASH) $(BENCH) >"$$d" 2>&1 ) 2>&1; \
		printf "  %-10s" "$(BASH)"; ( time SCALE=$$scale $(BASH) $(BENCH) >"$$b" 2>&1 ) 2>&1; \
		printf "  %-10s" "$(ZSH)";  ( time SCALE=$$scale $(ZSH)  $(BENCH) >"$$z" 2>&1 ) 2>&1; \
		printf "  %-10s" "$(GOSH)"; ( time SCALE=$$scale $(GOSH) $(BENCH) >"$$g" 2>&1 ) 2>&1; \
		printf "  %-10s" "$(BIN)";  ( time SCALE=$$scale $(BIN) --posix $(BENCH) >"$$s" 2>&1 ) 2>&1; \
		if cmp "$$d" "$$s"; then echo "output matches dash"; \
		else echo "output differs from dash:"; diff "$$d" "$$s" | head -20; fi; \
		echo "configure.bash, wall-clock seconds at SCALE=$$scale, lower is better:"; \
		printf "  %-10s" "$(BASH)"; ( time SCALE=$$scale $(BASH) $(BENCH_BASH) >"$$bb" 2>&1 ) 2>&1; \
		printf "  %-10s" "$(BIN)";  ( time SCALE=$$scale $(BIN) --bash-compatible $(BENCH_BASH) >"$$sb" 2>&1 ) 2>&1; \
		if cmp "$$bb" "$$sb"; then echo "output matches bash"; \
		else echo "output differs from bash:"; diff "$$bb" "$$sb" | head -20; fi; \
		rm -f "$$d" "$$b" "$$z" "$$g" "$$s" "$$bb" "$$sb"'

.PHONY: test clean run_tests refill dashdiff bashdiff mimicrydiff bench
