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


TEST_NAMES := $(INPUT_FILES:shit/%.shit=%)

%: expected/%.out
	@out=$$(mktemp); \
	cat shit/$@.shit | $(BIN) $(BIN_FLAGS) - > $$out 2>&1; \
	if diff $(DIFF_FLAGS) $< $$out >/dev/null 2>&1 || \
	   { [ -f expected/$@_1.out ] && \
	     diff $(DIFF_FLAGS) expected/$@_1.out $$out >/dev/null 2>&1; }; then \
		printf "	%-64s ok\r" $@; \
	else \
		diff $(DIFF_FLAGS) $< $$out >> $(FAILED_LIST); \
		printf "	%-64s FAILED :c\n" $@; \
	fi; \
	rm -f $$out

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

shit_tests: $(TEST_NAMES)


CLI_INPUT := $(wildcard cli/*.sh)
CLI_NAMES := $(CLI_INPUT:cli/%.sh=cli_%)

cli_%: expected/cli/%.out
	@out=$$(mktemp); \
	BIN=$(BIN) sh cli/$*.sh > $$out 2>&1; \
	if diff $(DIFF_FLAGS) $< $$out >/dev/null 2>&1; then \
		printf "	%-64s ok\r" cli/$*.sh; \
	else \
		diff $(DIFF_FLAGS) $< $$out >> $(FAILED_LIST); \
		printf "	%-64s FAILED :c\n" cli/$*.sh; \
	fi; \
	rm -f $$out

cli_tests: $(CLI_NAMES)


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


ZSH ?= zsh
GOSH ?= gosh
BASH ?= bash
DASH ?= dash

SH_COMPAT := $(filter-out %_1.sh,$(wildcard sh/*.sh))

dashdiff bashdiff mimicrydiff: SHELL := bash

dashdiff: $(BIN)
	@command -v $(DASH) >/dev/null 2>&1 || \
		{ printf "	%-64s skipped, no $(DASH)\n" dashdiff; exit 0; }; \
	for f in $(SH_COMPAT); do \
		s="$$($(BIN) --mood sh "$$f" 2>/dev/null; printf X)"; s="$${s%X}"; \
		d="$$($(DASH) "$$f" 2>/dev/null; printf X)"; d="$${d%X}"; \
		alt="$${f%.sh}_1.sh"; \
		if [ "$$s" = "$$d" ]; then \
			printf "	%-64s ok\r" "$$f"; \
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


BASH_COMPAT := $(filter-out %_1.bash,$(wildcard bash/*.bash))

bashdiff: $(BIN)
	@command -v $(BASH) >/dev/null 2>&1 || \
		{ printf "	%-64s skipped, no $(BASH)\n" bashdiff; exit 0; }; \
	for f in $(BASH_COMPAT); do \
		s="$$($(BIN) --mood bash "$$f" 2>/dev/null; printf X)"; s="$${s%X}"; \
		b="$$($(BASH) "$$f" 2>/dev/null; printf X)"; b="$${b%X}"; \
		alt="$${f%.bash}_1.bash"; \
		if [ "$$s" = "$$b" ]; then \
			printf "	%-64s ok\r" "$$f"; \
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
			printf "	%-64s mimic ok\r" "$$f"; \
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
			printf "	%-64s mimic ok\r" "$$f"; \
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

BENCH := bench/configure.sh
BENCH_BASH := bench/configure.bash
BENCH_SHIT := bench/configure.shit
SCALE ?= 100

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

.PHONY: test clean shit_tests refill dashdiff bashdiff mimicrydiff bench \
		completion_tests completion_refill cli_tests
