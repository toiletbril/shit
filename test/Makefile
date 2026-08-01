ifeq ($(OS), Windows_NT)
SHELL := sh
TEST_ABSOLUTE_DIRECTORY_COMMAND := pwd -W
else
SHELL := /bin/bash
TEST_ABSOLUTE_DIRECTORY_COMMAND := pwd
endif

TEST_HOST_MAKE ?= $(shell command -v "$(MAKE)")
ifeq ($(strip $(TEST_HOST_MAKE)),)
$(error The test make '$(MAKE)' could not be resolved)
endif
export TEST_HOST_MAKE

DIFF_FLAGS := -u
MODE ?= dbg
IS_NONDEBUG_BUILD := $(if $(filter rel prof cov cosmo,$(MODE)),1,0)
export IS_NONDEBUG_BUILD
ifeq ($(OS), Windows_NT)
TARGET ?= Windows_NT
else
TARGET ?= $(shell uname -s)
endif

ifeq ($(TARGET), Windows_NT)
.NOTPARALLEL:
export TEST_PATH_SEPARATOR := ;
export TEST_NULL_DEVICE := NUL
else
export TEST_PATH_SEPARATOR := :
export TEST_NULL_DEVICE := /dev/null
endif
export TEST_TEMP_DIRECTORY := .test-work

MODE_SUFFIX := $(if $(filter rel,$(MODE)),,-$(MODE))
ifeq ($(MODE), cosmo)
BIN ?= ../shit$(MODE_SUFFIX).com
else ifeq ($(TARGET), Windows_NT)
BIN ?= ../shit$(MODE_SUFFIX).exe
DIFF_FLAGS += -w -a # CRLF on Windows SMH
else
BIN ?= ../shit$(MODE_SUFFIX)
endif

# The test drivers run with the current directory set to test, so a BIN given
# relative to the repo root or as a bare name is resolved to an absolute path
# here. A path that exists as given wins, then the repo-root sibling, then a
# PATH lookup, so the target binary is found no matter where make was launched.
override BIN := $(shell \
	if [ -x "$(BIN)" ]; then \
		echo "$$(cd "$$(dirname "$(BIN)")" && $(TEST_ABSOLUTE_DIRECTORY_COMMAND))/$$(basename "$(BIN)")"; \
	elif [ -x "$(CURDIR)/../$(BIN)" ]; then \
		candidate="$(CURDIR)/../$(BIN)"; \
		echo "$$(cd "$$(dirname "$$candidate")" && $(TEST_ABSOLUTE_DIRECTORY_COMMAND))/$$(basename "$$candidate")"; \
	elif command -v "$(BIN)" >/dev/null 2>&1; then \
		found=$$(command -v "$(BIN)"); \
		echo "$$(cd "$$(dirname "$$found")" && $(TEST_ABSOLUTE_DIRECTORY_COMMAND))/$$(basename "$$found")"; \
	else echo "$(BIN)"; fi)
export BIN

BIN_FLAGS := -AER

export SHIT_HISTORY := $(CURDIR)/.test_history
export SHIT_DIRECTORY_HISTORY := $(CURDIR)/.test_directory_history
ifeq ($(TARGET), Windows_NT)
export TEST_SYSTEM_PATH := .test-work/system-path
export TEST_UNAME_DIRECTORY := .test-work/system-path
else
export TEST_SYSTEM_PATH := $(shell \
	shell_path=$$(command -v sh); printf '%s' "$${shell_path%/*}")
export TEST_UNAME_DIRECTORY := $(shell \
	uname_path=$$(command -v uname); printf '%s' "$${uname_path%/*}")
endif
export PATH := $(CURDIR)/bin$(TEST_PATH_SEPARATOR)$(PATH)

FAILED_LIST := failed.diff
INPUT_FILES := $(wildcard shit/*.shit)

TEST_NAMES := $(INPUT_FILES:shit/%.shit=%)

%: expected/%.out
	@BIN='$(BIN)' BIN_FLAGS='$(BIN_FLAGS)' DIFF_FLAGS='$(DIFF_FLAGS)' \
		FAILED_LIST='$(FAILED_LIST)' $(SHELL) run-shit-test.sh $*

clean:
	@rm -f $(FAILED_LIST) $(SHIT_HISTORY) $(SHIT_DIRECTORY_HISTORY)
	@test -n "$(CURDIR)/.test-work" && rm -rf "$(CURDIR)/.test-work"

TEST_SUITES := shit_tests cli_tests completion_tests highlight_tests
ifneq ($(TARGET), Windows_NT)
TEST_SUITES += diagnostic_tty_test dashdiff bashdiff mimicrydiff
endif

test: $(BIN)
	+@"$(TEST_HOST_MAKE)" clean
	+@"$(TEST_HOST_MAKE)" $(filter-out completion_tests,$(TEST_SUITES))
	+@"$(TEST_HOST_MAKE)" completion_tests
	+@for serial_test_name in $(SERIAL_TEST_NAMES); do \
		printf "	%-64s running\n" "$$serial_test_name"; \
		"$(TEST_HOST_MAKE)" --no-print-directory "$$serial_test_name" || exit 1; \
	done
	@touch $(FAILED_LIST)
	@cat $(FAILED_LIST)
	@test ! -s $(FAILED_LIST)

shit_tests: $(BIN) $(TEST_NAMES)

CLI_INPUT := $(wildcard cli/*.sh)
CLI_NAMES := $(CLI_INPUT:cli/%.sh=cli_%)
SERIAL_TEST_NAMES := cli_set_option_state cli_read_timeout

cli_%: expected/cli/%.out
	@BIN='$(BIN)' DIFF_FLAGS='$(DIFF_FLAGS)' FAILED_LIST='$(FAILED_LIST)' \
		$(SHELL) run-cli-test.sh '$(SHELL)' cli/$*.sh

cli_tests: $(BIN) $(filter-out $(SERIAL_TEST_NAMES),$(CLI_NAMES))


COMPLETION_INPUT := $(wildcard completion/*.sh)
COMPLETION_NAMES := $(COMPLETION_INPUT:completion/%.sh=completion_%)
SERIAL_COMPLETION_TEST_NAMES := completion_editor_append_hot_path

completion_%: expected/completion/%.out
	@if [ '$(IS_NONDEBUG_BUILD)' = 1 ]; then \
		printf "	%-64s skipped, release binary\n" completion/$*.sh; \
	else \
		out=$$(mktemp); \
		BIN='$(BIN)' '$(SHELL)' completion/$*.sh > $$out 2>/dev/null; \
		if diff $(DIFF_FLAGS) $< $$out >/dev/null 2>&1; then \
			printf "	%-64s ok\033[K\r" completion/$*.sh; \
		else \
			diff $(DIFF_FLAGS) $< $$out >> $(FAILED_LIST); \
			printf "	%-64s FAILED :c\n" completion/$*.sh; \
		fi; \
		rm -f $$out; \
	fi

ifeq ($(TARGET), Windows_NT)
WINDOWS_SKIPPED_TEST_NAMES := \
	ast_process_substitution \
	ast_redirection_varieties \
	heredoc_fd_three \
	lex_process_substitution \
	mood_conditional \
	redirect_csh_both_streams \
	redirect_dynamic_fd \
	redirect_high_fd \
	redirections \
	rerr_redirect_to_directory \
	umask_modes

$(WINDOWS_SKIPPED_TEST_NAMES):
	@printf "	%-64s skipped, unsupported Windows backend feature\n" "$@.shit"

cli_assimilate:
	@printf "	%-64s skipped, remote transaction requires POSIX\n" "cli/assimilate.sh"

cli_bash_xtracefd:
	@printf "	%-64s skipped, numbered descriptors are unsupported on Windows\n" \
		"cli/bash_xtracefd.sh"

WINDOWS_SKIPPED_CLI_NAMES := \
	cli_command_substitution_interrupt \
	cli_command_substitution_strategy \
	cli_cpu_count \
	cli_ctrl_c_interrupts_bash_loop \
	cli_deep_command_substitution_guard \
	cli_deep_parameter_expansion_guard \
	cli_diagnostic_levels \
	cli_disown_running_only \
	cli_exec_exit_codes \
	cli_exec_failed_interactive_state \
	cli_fg_terminal_handoff \
	cli_job_state_running_then_stopped \
	cli_mkdir_mode_umask \
	cli_path_cache_behavior \
	cli_regex_error_caret \
	cli_signal_noninteractive_term \
	cli_set_option_state \
	cli_spawn_failure_location \
	cli_stopped_job_survives_poll \
	cli_suspend_behavior \
	cli_wait_on_stopped_job

$(WINDOWS_SKIPPED_CLI_NAMES):
	@printf "	%-64s skipped, unsupported Windows backend feature\n" \
		"cli/$(patsubst cli_%,%,$@).sh"

WINDOWS_SKIPPED_COMPLETION_NAMES := \
	completion_complete_git_grouped_subcommands \
	completion_complete_go_subcommands \
	completion_complete_help_subcommands \
	completion_editor_append_hot_path \
	completion_help_options_trusted_dir_only \
	completion_help_subcommand_second_level \
	completion_help_subcommand_third_level \
	completion_help_subcommands_no_colon

$(WINDOWS_SKIPPED_COMPLETION_NAMES):
	@printf "	%-64s skipped, needs a POSIX helper executable\n" "$@"

WINDOWS_UNREPRESENTABLE_COMPLETION_NAMES := \
	completion_completion_glob_quoting \
	completion_path_quoting

$(WINDOWS_UNREPRESENTABLE_COMPLETION_NAMES):
	@printf "	%-64s skipped, golden test filenames are unsupported on Windows\n" \
		"$(patsubst completion_%,completion/%,$@).sh"
endif

completion_tests: $(BIN)
	+@if [ '$(IS_NONDEBUG_BUILD)' = 1 ]; then \
		printf "	%-64s skipped, release binary\n" completion; \
	else \
		"$(TEST_HOST_MAKE)" --no-print-directory \
			$(filter-out $(SERIAL_COMPLETION_TEST_NAMES),$(COMPLETION_NAMES)) \
			BIN='$(BIN)' DIFF_FLAGS='$(DIFF_FLAGS)' \
			FAILED_LIST='$(FAILED_LIST)' || exit 1; \
		for serial_test_name in $(SERIAL_COMPLETION_TEST_NAMES); do \
			printf "	%-64s running\n" "$$serial_test_name"; \
			"$(TEST_HOST_MAKE)" --no-print-directory \
				"$$serial_test_name" || exit 1; \
		done; \
	fi


HIGHLIGHT_INPUT := $(wildcard highlight/*.sh)
HIGHLIGHT_NAMES := $(HIGHLIGHT_INPUT:highlight/%.sh=highlight_%)

highlight_%: expected/highlight/%.out
	@BIN='$(BIN)' DIFF_FLAGS='$(DIFF_FLAGS)' FAILED_LIST='$(FAILED_LIST)' \
		$(SHELL) run-highlight-test.sh '$(SHELL)' highlight/$*.sh

highlight_tests: $(BIN)
	@BIN='$(BIN)' DIFF_FLAGS='$(DIFF_FLAGS)' FAILED_LIST='$(FAILED_LIST)' \
		$(SHELL) run-highlight-test.sh '$(SHELL)' $(HIGHLIGHT_INPUT)

DIAGNOSTIC_TTY_INPUT := \
	interactive/long_warning_window.py \
	interactive/cat_syntax_highlighting.py \
	interactive/underline_term_support.py

diagnostic_tty_test: $(BIN)
	@if command -v python3 >/dev/null 2>&1; then \
		for test_file in $(DIAGNOSTIC_TTY_INPUT); do \
			if [ "$$test_file" = interactive/long_warning_window.py ] && \
				[ '$(IS_NONDEBUG_BUILD)' = 1 ]; then \
				printf "\t%-64s skipped, release binary\n" "$$test_file"; \
				continue; \
			fi; \
			output=$$(mktemp) || exit; \
			if python3 "$$test_file" "$(BIN)" >"$$output" 2>&1; then \
				printf "\t%-64s ok\033[K\r" "$$test_file"; \
			else \
				cat "$$output"; \
				printf "\t%-64s FAILED :c\n" "$$test_file"; \
				rm -f "$$output"; \
				exit 1; \
			fi; \
			rm -f "$$output"; \
		done; \
	else \
		for test_file in $(DIAGNOSTIC_TTY_INPUT); do \
			printf "\t%-64s skipped, python3 unavailable\n" "$$test_file"; \
		done; \
	fi


REFILL ?=

refill:
	for f in $${REFILL:-$(TEST_NAMES)}; do \
		[ -f shit/$$f.shit ] || continue; \
		printf "	%-64s $$f.out\n" $$f.shit; \
		cat shit/$$f.shit | '$(BIN)' $(BIN_FLAGS) - > expected/$$f.out 2>&1; \
	done || true
	for f in $${REFILL:-$(CLI_INPUT:cli/%.sh=%)}; do \
		[ -f cli/$$f.sh ] || continue; \
		printf "	%-64s cli/$$f.out\n" cli/$$f.sh; \
		case $$f in \
		command_substitution_interrupt|fg_terminal_handoff|read_timeout|shitbox_timeout|transaction_lock_lifetime|wait_on_stopped_job) \
			golden_timeout_seconds=60; \
			[ "$$f" != shitbox_timeout ] || golden_timeout_seconds=120; \
			output=$$(mktemp "expected/cli/.$$f.out.XXXXXX") || exit; \
			CLI_TEST_TIMEOUT_SECONDS=$${CLI_TEST_TIMEOUT_SECONDS:-$$golden_timeout_seconds} \
				BIN='$(BIN)' '$(SHELL)' \
				./.run-bounded-cli-golden.sh cli/$$f.sh \
				> "$$output" 2>&1 && \
				mv "$$output" "expected/cli/$$f.out" || \
				{ status=$$?; rm -f "$$output"; exit $$status; } ;; \
		*) BIN='$(BIN)' '$(SHELL)' cli/$$f.sh > expected/cli/$$f.out 2>&1 ;; \
		esac; \
	done
	for f in $(COMPLETION_INPUT:completion/%.sh=%); do \
		printf "	%-64s $$f.out\n" completion/$$f.sh; \
		BIN='$(BIN)' '$(SHELL)' completion/$$f.sh > expected/completion/$$f.out 2>/dev/null; \
	done || true
	for f in $(HIGHLIGHT_INPUT:highlight/%.sh=%); do \
		printf "	%-64s $$f.out\n" highlight/$$f.sh; \
		BIN='$(BIN)' '$(SHELL)' highlight/$$f.sh > expected/highlight/$$f.out 2>/dev/null; \
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
