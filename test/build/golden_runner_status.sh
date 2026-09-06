#!/bin/bash
#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# This script verifies that golden runners preserve captured output and continue
# refill batches after a fixture exits with a nonzero status.

directory=$(mktemp -d) || exit 1
cleanup()
{
  if [ -n "$directory" ]; then
    "$TEST_SYSTEM_RM" -rf -- "$directory"
  fi
}
trap cleanup EXIT

# Prints the first label when the observed value equals the expected one and
# the second label otherwise.
report_value()
{
  if [ "$1" = "$2" ]; then
    echo "$3"
  else
    echo "$4"
  fi
}

# Prints the first label when the named file holds exactly the expected text.
report_file()
{
  report_value "$(command cat "$1")" "$2" "$3" "$4"
}

# Reports whether the named failure list recorded any fixture diff. A golden
# runner writes one file per failing fixture into a directory beside the list,
# and only the suite driver merges those files into the list itself.
has_recorded_diff()
{
  if [ -s "$1" ]; then
    return 0
  fi

  for RECORDED_DIFF in "$1.d"/*.diff; do
    if [ -s "$RECORDED_DIFF" ]; then
      return 0
    fi
  done

  return 1
}

# Runs the CLI golden runner from the scratch directory. CLI_TIMEOUT_SECONDS
# selects a bounded run and stays empty for an unbounded one. The remaining
# arguments are the runner operands.
run_cli_runner()
{
  RUN_FAILED_LIST=$1
  RUN_OUTPUT=$2
  shift 2
  (
    cd "$directory" || exit 1
    BIN="$BIN" \
    CLI_TEST_TIMEOUT_SECONDS="$CLI_TIMEOUT_SECONDS" \
    DIFF_FLAGS="$DIFF_FLAGS" \
    FAILED_LIST="$directory/$RUN_FAILED_LIST" \
    TEST_TEMP_DIRECTORY="$directory/work" \
      "$TEST_SHELL" run-cli-test.sh "$@" > "$RUN_OUTPUT"
  )
}

CLI_TIMEOUT_SECONDS=

mkdir -p "$directory/cli" "$directory/completion" "$directory/expected" \
  "$directory/highlight" "$directory/kosh" "$directory/suite/bash" \
  "$directory/suite/build" "$directory/suite/cli" \
  "$directory/suite/completion" "$directory/suite/highlight" \
  "$directory/suite/interactive" "$directory/suite/kosh" \
  "$directory/suite/sh"
printf '%s\n' '#!/bin/sh' 'printf "alpha-output\n"' 'exit 3' \
  > "$directory/cli/runner_failure.sh"
printf '%s\n' '#!/bin/sh' 'printf "zulu-output\n"' \
  > "$directory/cli/zzz_ok.sh"
printf 'alpha-output\n' > "$directory/expected/runner_failure.out"
printf 'zulu-output\n' > "$directory/expected/zzz_ok.out"

cp run-bounded-cli-golden.sh run-cli-test.sh run-completion-test.sh \
  run-highlight-test.sh run-kosh-test.sh run-refill.sh runner-status.sh \
  "$directory"
cp run-test-suite.sh "$directory/suite"
printf '%s\n' '#!/bin/sh' \
  'printf "cli:%s\\n" "$2" >> "$SUITE_TRACE"' \
  'case $2 in cli/a_failure.sh) exit 143 ;; esac' \
  > "$directory/suite/run-cli-test.sh"
printf '%s\n' '#!/bin/sh' \
  'printf "completion:%s\\n" "$2" >> "$SUITE_TRACE"' \
  'case $2 in completion/a_failure.sh) exit 7 ;; esac' \
  > "$directory/suite/run-completion-test.sh"
printf '%s\n' '#!/bin/sh' \
  'printf "kosh:%s\\n" "$1" >> "$SUITE_TRACE"' 'exit 7' \
  > "$directory/suite/run-kosh-test.sh"
printf '%s\n' '#!/bin/sh' \
  'printf "build:%s\\n" "$2" >> "$SUITE_TRACE"' \
  'printf "suite-failed-list-marker\\n" >> "$FAILED_LIST"' \
  > "$directory/suite/run-build-test.sh"
printf '%s\n' '#!/bin/sh' \
  'printf "highlight:%s\\n" "$2" >> "$SUITE_TRACE"' \
  > "$directory/suite/run-highlight-test.sh"
printf '%s\n' '#!/bin/sh' \
  'printf "compat\\n" >> "$SUITE_TRACE"' \
  > "$directory/suite/run-compat-diff-test.sh"
printf '%s\n' '#!/bin/sh' \
  'printf "interactive\\n" >> "$SUITE_TRACE"' \
  > "$directory/suite/run-interactive-test.sh"
for SUITE_INPUT in cli/a_failure.sh cli/b_after.sh cli/read_behavior.sh \
  completion/a_failure.sh completion/b_after.sh \
  completion/editor_append_hot_path.sh build/after.sh highlight/after.sh \
  interactive/after.py kosh/after.kosh sh/after.sh bash/after.bash; do
  printf '\n' > "$directory/suite/$SUITE_INPUT"
done

(
  cd "$directory/suite" || exit 1
  : > "$directory/suite-trace.log"
  BASHP="$TEST_SHELL" \
  BIN="$BIN" \
  DASH="$TEST_SHELL" \
  DIFF_FLAGS="$DIFF_FLAGS" \
  FAILED_LIST="$directory/suite-failed.diff" \
  KOSH_DIRECTORY_HISTORY="$directory/suite-directory-history" \
  KOSH_HISTORY_FILE="$directory/suite-history" \
  SKIPPED_BUILD_INPUT= \
  SKIPPED_CLI_INPUT= \
  SKIPPED_COMPLETION_INPUT= \
  SKIPPED_TEST_NAMES= \
  SUITE_TRACE="$directory/suite-trace.log" \
  TEST_JOBS=1 \
  TEST_NULL_DEVICE="$TEST_NULL_DEVICE" \
  TEST_SHELL="$TEST_SHELL" \
  TEST_SYSTEM_RM="$TEST_SYSTEM_RM" \
  TEST_TEMP_DIRECTORY="$directory/suite-work" \
  UNREPRESENTABLE_COMPLETION_INPUT= \
    "$TEST_SHELL" run-test-suite.sh all > "$directory/suite.log"
)
SUITE_STATUS=$?
report_value "$SUITE_STATUS" 143 suite-runner-retains-first-status \
  suite-runner-loses-first-status
if grep -Fqx 'cli:cli/b_after.sh' "$directory/suite-trace.log" && \
  grep -Fqx 'cli:cli/read_behavior.sh' "$directory/suite-trace.log"; then
  echo suite-runner-continues-cli-batches
else
  echo suite-runner-stops-cli-batches
fi
if grep -Fqx 'completion:completion/b_after.sh' \
  "$directory/suite-trace.log" && \
  grep -Fqx 'completion:completion/editor_append_hot_path.sh' \
  "$directory/suite-trace.log"; then
  echo suite-runner-continues-completion-batches
else
  echo suite-runner-stops-completion-batches
fi
if grep -Fqx 'kosh:after' "$directory/suite-trace.log" && \
  grep -Fqx 'build:build/after.sh' "$directory/suite-trace.log" && \
  grep -Fqx 'highlight:highlight/after.sh' "$directory/suite-trace.log" && \
  grep -Fqx 'completion:completion/a_failure.sh' \
  "$directory/suite-trace.log" && \
  grep -Fqx 'compat' "$directory/suite-trace.log" && \
  grep -Fqx 'interactive' "$directory/suite-trace.log"; then
  echo suite-runner-continues-harnesses
else
  echo suite-runner-stops-harnesses
fi
if grep -Fqx 'suite-failed-list-marker' "$directory/suite-failed.diff"; then
  echo suite-runner-aggregates-failed-list
else
  echo suite-runner-loses-failed-list
fi

run_cli_runner failed.diff "$directory/comparison.log" "$TEST_SHELL" \
  cli/runner_failure.sh cli/zzz_ok.sh
COMPARISON_STATUS=$?
report_value "$COMPARISON_STATUS" 0 \
  comparison-runner-accepts-matching-nonzero-fixture \
  comparison-runner-rejects-matching-nonzero-fixture

printf 'stale-output\n' > "$directory/expected/runner_failure.out"
run_cli_runner mismatch-failed.diff "$directory/mismatch.log" "$TEST_SHELL" \
  cli/runner_failure.sh
MISMATCH_STATUS=$?
report_value "$MISMATCH_STATUS" 1 \
  comparison-runner-rejects-mismatching-nonzero-fixture \
  comparison-runner-accepts-mismatching-nonzero-fixture
if has_recorded_diff "$directory/mismatch-failed.diff"; then
  echo comparison-runner-compares-nonzero-fixture
else
  echo comparison-runner-skips-nonzero-fixture
fi

printf 'stale-output\n' > "$directory/expected/runner_failure.out"
printf 'stale-output\n' > "$directory/expected/zzz_ok.out"
run_cli_runner refill-batch-failed.diff "$TEST_NULL_DEVICE" --refill \
  "$TEST_SHELL" cli/runner_failure.sh cli/zzz_ok.sh
REFILL_STATUS=$?
report_value "$REFILL_STATUS" 0 refill-runner-accepts-nonzero-fixture \
  refill-runner-rejects-nonzero-fixture

report_file "$directory/expected/runner_failure.out" alpha-output \
  refill-runner-records-nonzero-output refill-runner-missed-nonzero-output
report_file "$directory/expected/zzz_ok.out" zulu-output \
  refill-runner-continues-batch refill-runner-stops-batch

printf 'wrapper-stale-output\n' > "$directory/expected/runner_failure.out"
printf 'wrapper-stale-output\n' > "$directory/expected/zzz_ok.out"
(
  cd "$directory" || exit 1
  BIN="$BIN" \
  BIN_FLAGS= \
  DIFF_FLAGS="$DIFF_FLAGS" \
  FAILED_LIST="$directory/refill-failed.diff" \
  REFILL='runner_failure zzz_ok' \
  TEST_TEMP_DIRECTORY="$directory/work" \
    "$TEST_SHELL" run-refill.sh "$TEST_SHELL" >"$TEST_NULL_DEVICE"
)
WRAPPER_STATUS=$?
report_value "$WRAPPER_STATUS" 0 refill-wrapper-accepts-nonzero-fixture \
  refill-wrapper-rejects-nonzero-fixture

report_file "$directory/expected/runner_failure.out" alpha-output \
  refill-wrapper-records-nonzero-output refill-wrapper-misses-nonzero-output
report_file "$directory/expected/zzz_ok.out" zulu-output \
  refill-wrapper-continues-batch refill-wrapper-stops-batch

printf '%s\n' '#!/bin/sh' 'printf "partial-output\n"' \
  '"$BIN" -c "koshkit sleep 10"' > "$directory/cli/runner_failure.sh"
printf 'stable-output\n' > "$directory/expected/runner_failure.out"
CLI_TIMEOUT_SECONDS=2
run_cli_runner comparison-timeout-failed.diff \
  "$directory/comparison-timeout.log" "$TEST_SHELL" cli/runner_failure.sh
COMPARISON_TIMEOUT_STATUS=$?
report_value "$COMPARISON_TIMEOUT_STATUS" 124 \
  comparison-runner-preserves-timeout-status \
  comparison-runner-loses-timeout-status
printf 'partial-output\ngolden timed out\n\t%-64s harness failure, status 124\n' \
  'cli/runner_failure.sh' > "$directory/expected-timeout.log"
if diff -u "$directory/expected-timeout.log" \
  "$directory/comparison-timeout.log" >"$TEST_NULL_DEVICE" 2>&1; then
  echo comparison-runner-preserves-timeout-diagnostic
else
  echo comparison-runner-loses-timeout-diagnostic
fi

printf 'stale-output\n' > "$directory/expected/zzz_ok.out"
run_cli_runner comparison-continuation-failed.diff "$TEST_NULL_DEVICE" \
  "$TEST_SHELL" cli/runner_failure.sh cli/zzz_ok.sh
COMPARISON_CONTINUATION_STATUS=$?
report_value "$COMPARISON_CONTINUATION_STATUS" 124 \
  comparison-runner-retains-timeout-before-mismatch \
  comparison-runner-loses-timeout-before-mismatch
if has_recorded_diff "$directory/comparison-continuation-failed.diff"; then
  echo comparison-runner-continues-after-timeout
else
  echo comparison-runner-stops-after-timeout
fi

printf 'stable-output\n' > "$directory/expected/runner_failure.out"
printf 'stale-output\n' > "$directory/expected/zzz_ok.out"
run_cli_runner timeout-failed.diff "$TEST_NULL_DEVICE" --refill "$TEST_SHELL" \
  cli/runner_failure.sh cli/zzz_ok.sh
REFILL_TIMEOUT_STATUS=$?
report_value "$REFILL_TIMEOUT_STATUS" 124 refill-runner-preserves-timeout-status \
  refill-runner-loses-timeout-status
report_file "$directory/expected/runner_failure.out" stable-output \
  refill-runner-preserves-golden-after-timeout \
  refill-runner-replaces-golden-after-timeout
report_file "$directory/expected/zzz_ok.out" zulu-output \
  refill-runner-continues-after-timeout refill-runner-stops-after-timeout

printf 'stable-output\n' > "$directory/expected/runner_failure.out"
printf 'stale-output\n' > "$directory/expected/zzz_ok.out"
(
  cd "$directory" || exit 1
  BIN="$BIN" \
  BIN_FLAGS= \
  CLI_TEST_TIMEOUT_SECONDS=2 \
  DIFF_FLAGS="$DIFF_FLAGS" \
  FAILED_LIST="$directory/wrapper-timeout-failed.diff" \
  REFILL='runner_failure zzz_ok' \
  TEST_TEMP_DIRECTORY="$directory/work" \
    "$TEST_SHELL" run-refill.sh "$TEST_SHELL" >"$TEST_NULL_DEVICE"
)
WRAPPER_TIMEOUT_STATUS=$?
report_value "$WRAPPER_TIMEOUT_STATUS" 124 \
  refill-wrapper-preserves-timeout-status refill-wrapper-loses-timeout-status
report_file "$directory/expected/runner_failure.out" stable-output \
  refill-wrapper-preserves-golden-after-timeout \
  refill-wrapper-replaces-golden-after-timeout
report_file "$directory/expected/zzz_ok.out" zulu-output \
  refill-wrapper-continues-after-timeout refill-wrapper-stops-after-timeout

printf '%s\n' '#!/bin/sh' 'printf "should-not-run\n"' \
  > "$directory/cli/runner_failure.sh"
CLI_TIMEOUT_SECONDS=invalid
run_cli_runner invalid-timeout-failed.diff "$directory/invalid-timeout.log" \
  "$TEST_SHELL" cli/runner_failure.sh
INVALID_TIMEOUT_STATUS=$?
report_value "$INVALID_TIMEOUT_STATUS" 125 \
  comparison-runner-preserves-invalid-timeout-status \
  comparison-runner-loses-invalid-timeout-status
printf 'invalid CLI golden timeout\n\t%-64s harness failure, status 125\n' \
  'cli/runner_failure.sh' > "$directory/expected-invalid-timeout.log"
if diff -u "$directory/expected-invalid-timeout.log" \
  "$directory/invalid-timeout.log" >"$TEST_NULL_DEVICE" 2>&1; then
  echo comparison-runner-preserves-invalid-timeout-diagnostic
else
  echo comparison-runner-loses-invalid-timeout-diagnostic
fi

printf '%s\n' '#!/bin/sh' 'exit 143' > "$directory/cli/runner_failure.sh"
CLI_TIMEOUT_SECONDS=
run_cli_runner signal-failed.diff "$directory/signal.log" "$TEST_SHELL" \
  cli/runner_failure.sh
SIGNAL_STATUS=$?
report_value "$SIGNAL_STATUS" 143 comparison-runner-preserves-signal-status \
  comparison-runner-loses-signal-status
printf '\t%-64s harness failure, status 143\n' 'cli/runner_failure.sh' \
  > "$directory/expected-signal.log"
if diff -u "$directory/expected-signal.log" "$directory/signal.log" \
  >"$TEST_NULL_DEVICE" 2>&1; then
  echo comparison-runner-preserves-signal-diagnostic
else
  echo comparison-runner-loses-signal-diagnostic
fi

printf '%s\n' '#!/bin/sh' 'exit 143' > "$directory/cli/unbounded_signal.sh"
printf 'stable-output\n' > "$directory/expected/unbounded_signal.out"
run_cli_runner unbounded-signal-failed.diff \
  "$directory/unbounded-signal.log" "$TEST_SHELL" cli/unbounded_signal.sh
UNBOUNDED_SIGNAL_STATUS=$?
report_value "$UNBOUNDED_SIGNAL_STATUS" 143 \
  unbounded-comparison-preserves-signal-status \
  unbounded-comparison-loses-signal-status
printf '\t%-64s harness failure, status 143\n' 'cli/unbounded_signal.sh' \
  > "$directory/expected-unbounded-signal.log"
if diff -u "$directory/expected-unbounded-signal.log" \
  "$directory/unbounded-signal.log" >"$TEST_NULL_DEVICE" 2>&1; then
  echo unbounded-comparison-preserves-signal-diagnostic
else
  echo unbounded-comparison-loses-signal-diagnostic
fi

printf '%s\n' '#!/bin/sh' 'printf "missing-command-output\n"' 'exit 127' \
  > "$directory/cli/missing_command.sh"
printf 'missing-command-output\n' > "$directory/expected/missing_command.out"
run_cli_runner missing-command-failed.diff "$directory/missing-command.log" \
  "$TEST_SHELL" cli/missing_command.sh
MISSING_COMMAND_STATUS=$?
report_value "$MISSING_COMMAND_STATUS" 0 \
  comparison-runner-accepts-missing-command-fixture \
  comparison-runner-rejects-missing-command-fixture

for LAUNCH_FAILURE_CODE in 126 127; do
  printf '%s\n' '#!/bin/sh' 'printf "truncated-output\n"' \
    "exit $LAUNCH_FAILURE_CODE" > "$directory/cli/missing_command.sh"
  printf 'stable-output\n' > "$directory/expected/missing_command.out"
  run_cli_runner missing-command-refill-failed.diff "$TEST_NULL_DEVICE" \
    --refill "$TEST_SHELL" cli/missing_command.sh
  LAUNCH_FAILURE_STATUS=$?
  report_value "$LAUNCH_FAILURE_STATUS" "$LAUNCH_FAILURE_CODE" \
    "refill-runner-preserves-status-$LAUNCH_FAILURE_CODE" \
    "refill-runner-loses-status-$LAUNCH_FAILURE_CODE"
  report_file "$directory/expected/missing_command.out" stable-output \
    "refill-runner-preserves-golden-after-$LAUNCH_FAILURE_CODE" \
    "refill-runner-replaces-golden-after-$LAUNCH_FAILURE_CODE"
done

printf '%s\n' '#!/bin/sh' 'exit 143' > "$directory/failing-bin"
printf '%s\n' '# native source' > "$directory/kosh/native_signal.kosh"
printf 'stable-native\n' > "$directory/expected/native_signal.out"
chmod +x "$directory/failing-bin"
(
  cd "$directory" || exit 1
  BIN="$directory/failing-bin" \
  BIN_FLAGS= \
  DIFF_FLAGS="$DIFF_FLAGS" \
  FAILED_LIST="$directory/native-signal-failed.diff" \
  TEST_TEMP_DIRECTORY="$directory/work" \
    "$TEST_SHELL" run-kosh-test.sh --refill native_signal \
      > "$directory/native-signal.log"
)
NATIVE_SIGNAL_STATUS=$?
report_value "$NATIVE_SIGNAL_STATUS" 143 native-refill-preserves-signal-status \
  native-refill-loses-signal-status
report_file "$directory/expected/native_signal.out" stable-native \
  native-refill-preserves-golden-after-signal \
  native-refill-replaces-golden-after-signal

printf '%s\n' '#!/bin/sh' 'printf "ordinary-native\n"' 'exit 3' \
  > "$directory/ordinary-bin"
printf 'ordinary-native\n' > "$directory/expected/native_signal.out"
chmod +x "$directory/ordinary-bin"
(
  cd "$directory" || exit 1
  BIN="$directory/ordinary-bin" \
  BIN_FLAGS= \
  DIFF_FLAGS="$DIFF_FLAGS" \
  FAILED_LIST="$directory/native-ordinary-failed.diff" \
  TEST_TEMP_DIRECTORY="$directory/work" \
    "$TEST_SHELL" run-kosh-test.sh native_signal >"$TEST_NULL_DEVICE"
)
NATIVE_ORDINARY_STATUS=$?
report_value "$NATIVE_ORDINARY_STATUS" 0 \
  native-comparison-accepts-matching-nonzero-fixture \
  native-comparison-rejects-matching-nonzero-fixture

for RUNNER_CLASS in completion highlight; do
  printf '%s\n' '#!/bin/sh' 'exit 143' \
    > "$directory/$RUNNER_CLASS/producer_failure.sh"
  printf 'stable-%s\n' "$RUNNER_CLASS" \
    > "$directory/expected/producer_failure.out"
  (
    cd "$directory" || exit 1
    BIN="$BIN" \
    DIFF_FLAGS="$DIFF_FLAGS" \
    FAILED_LIST="$directory/$RUNNER_CLASS-failed.diff" \
    IS_NONDEBUG_BUILD=0 \
    TEST_TEMP_DIRECTORY="$directory/work" \
      "$TEST_SHELL" "run-$RUNNER_CLASS-test.sh" --refill "$TEST_SHELL" \
        "$RUNNER_CLASS/producer_failure.sh" \
        > "$directory/$RUNNER_CLASS-signal.log"
  )
  PRODUCER_SIGNAL_STATUS=$?
  report_value "$PRODUCER_SIGNAL_STATUS" 143 \
    "$RUNNER_CLASS-refill-preserves-signal-status" \
    "$RUNNER_CLASS-refill-loses-signal-status"
  report_file "$directory/expected/producer_failure.out" \
    "stable-$RUNNER_CLASS" \
    "$RUNNER_CLASS-refill-preserves-golden-after-signal" \
    "$RUNNER_CLASS-refill-replaces-golden-after-signal"

  printf '%s\n' '#!/bin/sh' 'printf "partial-producer-output\n"' 'exit 7' \
    > "$directory/$RUNNER_CLASS/producer_failure.sh"
  (
    cd "$directory" || exit 1
    BIN="$BIN" \
    DIFF_FLAGS="$DIFF_FLAGS" \
    FAILED_LIST="$directory/$RUNNER_CLASS-failed.diff" \
    IS_NONDEBUG_BUILD=0 \
    TEST_TEMP_DIRECTORY="$directory/work" \
      "$TEST_SHELL" "run-$RUNNER_CLASS-test.sh" --refill "$TEST_SHELL" \
        "$RUNNER_CLASS/producer_failure.sh" \
        > "$directory/$RUNNER_CLASS-ordinary.log"
  )
  PRODUCER_ORDINARY_STATUS=$?
  report_value "$PRODUCER_ORDINARY_STATUS" 0 \
    "$RUNNER_CLASS-refill-accepts-nonzero-producer" \
    "$RUNNER_CLASS-refill-rejects-nonzero-producer"
  report_file "$directory/expected/producer_failure.out" \
    partial-producer-output "$RUNNER_CLASS-refill-records-nonzero-output" \
    "$RUNNER_CLASS-refill-misses-nonzero-output"
done

(
  cd "$directory" || exit 1
  BIN="$BIN" \
  BIN_FLAGS= \
  DIFF_FLAGS="$DIFF_FLAGS" \
  FAILED_LIST="$directory/unknown-refill-failed.diff" \
  REFILL=missing_test \
  TEST_TEMP_DIRECTORY="$directory/work" \
    "$TEST_SHELL" run-refill.sh "$TEST_SHELL" \
      > "$directory/unknown-refill.log" 2>&1
)
UNKNOWN_REFILL_STATUS=$?
report_value "$UNKNOWN_REFILL_STATUS" 1 refill-wrapper-rejects-unknown-test \
  refill-wrapper-accepts-unknown-test
report_file "$directory/unknown-refill.log" \
  'Unknown refill test: missing_test' refill-wrapper-reports-unknown-test \
  refill-wrapper-hides-unknown-test
