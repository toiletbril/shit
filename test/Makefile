DIFF_FLAGS := -u

# The golden, cli, and completion suites need the debug binary, since the
# completion driver behind --debug-complete-at is gated behind NDEBUG, so the
# mode defaults to dbg. BIN names the artifact the build writes for this MODE and
# TARGET the same way src/Makefile does, rel with no suffix and every other mode
# with a -MODE suffix, plus the platform extension. The bench target overrides
# BIN to the optimized build below.
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

# The suite runs cd and z, which write the directory history, so both the
# command history and the directory history are redirected to ignored in-repo
# files. Running the suite then never reads or corrupts the developer's global
# ~/.shit_history or ~/.shit_directory_history.
export SHIT_HISTORY := $(CURDIR)/.test_history
export SHIT_DIRECTORY_HISTORY := $(CURDIR)/.test_directory_history

FAILED_LIST := failed.diff
INPUT_FILES := $(wildcard shit/*.shit)

TEST_NAMES := $(INPUT_FILES:shit/%.shit=%)

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
	@$(MAKE) -B cli_tests
	@$(MAKE) -B completion_tests
	@$(MAKE) -B dashdiff
	@$(MAKE) -B bashdiff
	@$(MAKE) -B mimicrydiff
	@touch $(FAILED_LIST)
	@cat $(FAILED_LIST)
	@test ! -s $(FAILED_LIST)

run_tests: $(TEST_NAMES)

# The CLI suite drives the binary through its flags and operands rather than
# through a piped script, so the flag parser, the help sections, the located
# invocation errors, and the SHIT_FLAGS environment stay covered. Each test is
# a POSIX sh script that reads the binary path from BIN and prints the output
# the golden in expected/cli records.
CLI_INPUT := $(wildcard cli/*.sh)
CLI_NAMES := $(CLI_INPUT:cli/%.sh=cli_%)

cli_%: expected/cli/%.out
	@out=$$(mktemp); \
	BIN=$(BIN) sh cli/$*.sh > $$out 2>&1; \
	if diff $(DIFF_FLAGS) $< $$out >/dev/null 2>&1; then \
		printf "	%-64s ok\n" cli/$*.sh; \
	else \
		diff $(DIFF_FLAGS) $< $$out >> $(FAILED_LIST); \
		printf "	%-64s FAILED :c\n" cli/$*.sh; \
	fi; \
	rm -f $$out

cli_tests: $(CLI_NAMES)

# The completion suite drives the engine through --debug-complete-at, the
# driver only a debug build compiles in, so the whole suite skips cleanly on
# a release binary. Each test prints candidates with stderr dropped, since
# the debug build traces there.
COMPLETION_INPUT := $(wildcard completion/*.sh)
COMPLETION_NAMES := $(COMPLETION_INPUT:completion/%.sh=completion_%)

completion_%: expected/completion/%.out
	@if ! $(BIN) --debug-complete-at '' </dev/null >/dev/null 2>&1; then \
		printf "	%-64s skipped, release binary\n" completion/$*.sh; \
	else \
		out=$$(mktemp); \
		BIN=$(BIN) sh completion/$*.sh > $$out 2>/dev/null; \
		if diff $(DIFF_FLAGS) $< $$out >/dev/null 2>&1; then \
			printf "	%-64s ok\n" completion/$*.sh; \
		else \
			diff $(DIFF_FLAGS) $< $$out >> $(FAILED_LIST); \
			printf "	%-64s FAILED :c\n" completion/$*.sh; \
		fi; \
		rm -f $$out; \
	fi

completion_tests: $(COMPLETION_NAMES)

completion_refill:
	for f in $(COMPLETION_INPUT:completion/%.sh=%); do \
		printf "	%-64s $$f.out\n" completion/$$f.sh; \
		BIN=$(BIN) sh completion/$$f.sh > expected/completion/$$f.out 2>/dev/null; \
	done || true

# REFILL names the tests to regenerate, space separated, and defaults to every
# test. A name regenerates only the suite whose input file exists, so it refills
# the main golden when shit/NAME.shit is present and the cli golden when
# cli/NAME.sh is present, and an unknown name is a quiet no-op. The main suite
# and the cli suite refill together, since the cli goldens used to need a
# separate target.
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
# The default mood is stricter than dash, a failing pipeline stage aborts and an
# unmatched glob is fatal, so the dash comparison runs shit with --mood sh where
# those strictnesses are off and the behavior tracks dash.
dashdiff: $(BIN)
	@command -v $(DASH) >/dev/null 2>&1 || \
		{ printf "	%-64s skipped, no $(DASH)\n" dashdiff; exit 0; }; \
	for f in $(SH_COMPAT); do \
		s="$$($(BIN) --mood sh "$$f" 2>/dev/null; printf X)"; s="$${s%X}"; \
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
	@command -v $(BASH) >/dev/null 2>&1 || \
		{ printf "	%-64s skipped, no $(BASH)\n" bashdiff; exit 0; }; \
	for f in $(BASH_COMPAT); do \
		s="$$($(BIN) --mood bash "$$f" 2>/dev/null; printf X)"; s="$${s%X}"; \
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
	@if command -v $(BASH) >/dev/null 2>&1; then for f in $(BASH_COMPAT); do \
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
	done; else printf "	%-64s skipped, no $(BASH)\n" mimicrydiff; fi; \
	if command -v $(DASH) >/dev/null 2>&1; then for f in $(SH_COMPAT); do \
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
	done; else printf "	%-64s skipped, no $(DASH)\n" mimicrydiff; fi

# Time the builtin-only configure script through each shell, after checking that
# the output is identical. Use the optimized shit build, MODE=rel, for a
# fair comparison. SCALE scales the work, the default keeps dash near 0.1s.
ZSH ?= zsh
GOSH ?= gosh

BENCH := bench/configure.sh
BENCH_BASH := bench/configure.bash
BENCH_SHIT := bench/configure.shit
SCALE ?= 100

# Each shell runs exactly once. The single timed pass redirects its output to a
# per-shell temp file, so the captured wall-clock and the captured output both
# come from that one run rather than running the shell again for the comparison.
# The POSIX script is checked against dash and the bash script against bash. cmp
# runs without -s so a mismatch names the first differing byte and line, then a
# diff shows the differing lines.
# The benchmark measures the optimized build, so BIN points at the rel artifact
# here whatever mode the rest of the suite ran under.
bench: BIN := ../shit
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
		printf "  %-10s" "$(BIN)";  ( time SCALE=$$scale $(BIN) --mood sh $(BENCH) >"$$s" 2>&1 ) 2>&1; \
		if cmp "$$d" "$$s"; then echo "output matches dash"; \
		else echo "output differs from dash:"; diff "$$d" "$$s" | head -20; fi; \
		printf "  %-10s" "$(BIN)+opt"; ( time SCALE=$$scale $(BIN) $(BENCH) >"$$s" 2>/dev/null ) 2>&1; \
		if cmp "$$d" "$$s"; then echo "output matches dash with the optimizer on"; \
		else echo "output differs from dash:"; diff "$$d" "$$s" | head -20; fi; \
		echo "configure.bash, wall-clock seconds at SCALE=$$scale, lower is better:"; \
		printf "  %-10s" "$(BASH)"; ( time SCALE=$$scale $(BASH) $(BENCH_BASH) >"$$bb" 2>&1 ) 2>&1; \
		printf "  %-10s" "$(BIN)";  ( time SCALE=$$scale $(BIN) --mood bash $(BENCH_BASH) >"$$sb" 2>&1 ) 2>&1; \
		if cmp "$$bb" "$$sb"; then echo "output matches bash"; \
		else echo "output differs from bash:"; diff "$$bb" "$$sb" | head -20; fi; \
		printf "  %-10s" "$(BIN)+opt"; ( time SCALE=$$scale $(BIN) $(BENCH_BASH) >"$$sb" 2>/dev/null ) 2>&1; \
		if cmp "$$bb" "$$sb"; then echo "output matches bash with the optimizer on"; \
		else echo "output differs from bash:"; diff "$$bb" "$$sb" | head -20; fi; \
		zb=/tmp/shit_bench_zb; zs=/tmp/shit_bench_zs; \
		echo "configure.shit, wall-clock seconds at SCALE=$$scale, lower is better:"; \
		printf "  %-10s" "$(BASH)"; ( time SCALE=$$scale $(BASH) $(BENCH_SHIT) >"$$zb" 2>&1 ) 2>&1; \
		printf "  %-10s" "$(BIN)+opt"; ( time SCALE=$$scale $(BIN) $(BENCH_SHIT) >"$$zs" 2>/dev/null ) 2>&1; \
		if cmp "$$zb" "$$zs"; then echo "output matches bash with the optimizer on"; \
		else echo "output differs from bash:"; diff "$$zb" "$$zs" | head -20; fi; \
		rm -f "$$d" "$$b" "$$z" "$$g" "$$s" "$$bb" "$$sb" "$$zb" "$$zs"'

.PHONY: test clean run_tests refill dashdiff bashdiff mimicrydiff bench \
		completion_tests completion_refill cli_tests
