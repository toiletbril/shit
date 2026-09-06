#!/bin/bash
. ./runner-status.sh

REFILL_MODE=no
TEST_STATUS=0
if [ "${1-}" = --refill ]; then
  REFILL_MODE=yes
  shift
fi

TEST_SHELL_COMMAND=$1
shift

for TEST_FILE in "$@"; do
  TEST_NAME=$(basename "$TEST_FILE" .sh)
  if [ "$REFILL_MODE" = yes ]; then
    OUTPUT="expected/.$TEST_NAME.out.tmp"
  else
    OUTPUT_DIRECTORY="$TEST_TEMP_DIRECTORY/results/build"
    mkdir -p "$OUTPUT_DIRECTORY"
    OUTPUT="$OUTPUT_DIRECTORY/$TEST_NAME.out"
  fi

  BIN="$BIN" "$TEST_SHELL_COMMAND" "$TEST_FILE" > "$OUTPUT" 2>&1
  DRIVER_STATUS=$?
  if is_driver_status_harness_failure "$DRIVER_STATUS" "$REFILL_MODE"; then
    command cat "$OUTPUT"
    printf "\t%-64s harness failure, status %s\n" \
      "build/$TEST_NAME.sh" "$DRIVER_STATUS"
    rm -f "$OUTPUT"
    if [ "$TEST_STATUS" -eq 0 ]; then
      TEST_STATUS=$DRIVER_STATUS
    fi
    continue
  fi

  if [ "$REFILL_MODE" = yes ]; then
    mv "$OUTPUT" "expected/$TEST_NAME.out"
    printf "\t%-64s %s.out\n" "build/$TEST_NAME.sh" "$TEST_NAME"
    continue
  fi

  if diff $DIFF_FLAGS "expected/$TEST_NAME.out" "$OUTPUT" >/dev/null 2>&1; then
    printf "\t%-64s ok\033[K\r" "build/$TEST_NAME.sh"
  else
    set_golden_failure_file "build-$TEST_NAME"
    diff $DIFF_FLAGS "expected/$TEST_NAME.out" "$OUTPUT" | \
      tee -a "$GOLDEN_FAILURE_FILE"
    printf "\t%-64s FAILED :c\n" "build/$TEST_NAME.sh"
    if [ "$TEST_STATUS" -eq 0 ]; then
      TEST_STATUS=1
    fi
  fi
  rm -f "$OUTPUT"
done

exit "$TEST_STATUS"
