DIFF_FLAGS := -u

ifeq ($(OS), Windows_NT)
BIN := ../shit.exe
DIFF_FLAGS += -w # CRLF on Windows SMH
else
BIN := ../shit
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
	printf "	%-42s ok\n" $@ || \
	printf "	%-42s FAILED :c\n" $@

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
		printf "	%-42s $$f.out\n" $$f.shit; \
		cat input/$$f.shit | $(BIN) $(BIN_FLAGS) - > expected/$$f.out 2>&1; \
	done || true

# Run each sh-compat script through the shell and through dash, then compare
# their standard output and exit status. The goal is byte-identical behavior.
DASH ?= dash
SH_COMPAT := $(wildcard sh-compat/*.shit)

dashdiff: $(BIN)
	@for f in $(SH_COMPAT); do \
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

# Run each bash-compat script through the shell in bash mode and through bash,
# then compare their standard output and exit status. The goal is byte-identical
# behavior with real bash for the bash extensions.
BASH ?= bash
BASH_COMPAT := $(wildcard bash-compat/*.shit)

bashdiff: $(BIN)
	@for f in $(BASH_COMPAT); do \
		$(BIN) --bash-compatible "$$f" >/tmp/shit_bd_s 2>/dev/null; shit_rc=$$?; \
		$(BASH) "$$f" >/tmp/shit_bd_b 2>/dev/null; bash_rc=$$?; \
		if cmp -s /tmp/shit_bd_s /tmp/shit_bd_b && [ "$$shit_rc" = "$$bash_rc" ]; then \
			printf "	%-42s ok\n" "$$f"; \
		else \
			printf "	%-42s DIFF (shit rc=%s bash rc=%s)\n" "$$f" "$$shit_rc" "$$bash_rc"; \
			diff /tmp/shit_bd_b /tmp/shit_bd_s | head -20; \
		fi; \
	done; \
	rm -f /tmp/shit_bd_s /tmp/shit_bd_b

# Time the builtin-only configure script through each shell, after checking that
# the output is identical. Use the optimized shit build, MODE=rel, for a
# fair comparison. SCALE scales the work, the default keeps dash near 0.1s.
ZSH ?= zsh
BENCH := bench/configure.shit
SCALE ?= 40

bench: $(BIN)
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
		for sh in "$(DASH)" "$(BASH)" "$(ZSH)" "$(BIN)"; do \
			printf "  %-10s" "$$sh"; \
			flags=; [ "$$sh" = "$(BIN)" ] && flags=--no-diagnostics; \
			( time SCALE=$(SCALE) $$sh $$flags $(BENCH) >/dev/null 2>&1 ) 2>&1; \
		done'; \
	rm -f /tmp/shit_bench_d /tmp/shit_bench_s

.PHONY: test clean run_tests refill dashdiff bashdiff bench
