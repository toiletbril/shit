#!/bin/bash
. ./runner-status.sh

REFILL_MODE=no
TEST_STATUS=0
NATIVE_TEST_TIMEOUT_SECONDS=${NATIVE_TEST_TIMEOUT_SECONDS:-60}
case $NATIVE_TEST_TIMEOUT_SECONDS in
''|*[!0-9]*|0)
  printf 'invalid native golden timeout\n'
  exit 125
  ;;
esac

if [ "${1-}" = --refill ]; then
  REFILL_MODE=yes
  shift
fi

for TEST_NAME in "$@"; do
  [ -f "kosh/$TEST_NAME.kosh" ] || continue
  case $TEST_NAME in
  shellcheck_static_*) TEST_BIN_FLAGS="$BIN_FLAGS -WWW" ;;
  *) TEST_BIN_FLAGS="$BIN_FLAGS -WWW --no-annoying-diagnostics" ;;
  esac
  if [ "$REFILL_MODE" = yes ]; then
    OUTPUT="expected/.$TEST_NAME.out.tmp"
  else
    OUTPUT_DIRECTORY="$TEST_TEMP_DIRECTORY/results/kosh"
    mkdir -p "$OUTPUT_DIRECTORY"
    OUTPUT="$OUTPUT_DIRECTORY/$TEST_NAME.out"
  fi

  KOSH_TEST_TIMEOUT_JOB_LIFETIME=leader \
    "$BIN" --no-diagnostics -c \
      'koshkit timeout "$1" "$2" $3 -' native-test \
      "$NATIVE_TEST_TIMEOUT_SECONDS" "$BIN" "$TEST_BIN_FLAGS" \
      < "kosh/$TEST_NAME.kosh" > "$OUTPUT" 2>&1
  DRIVER_STATUS=$?
  if is_driver_status_harness_failure "$DRIVER_STATUS" "$REFILL_MODE"; then
    command cat "$OUTPUT"
    printf "\t%-64s harness failure, status %s\n" \
      "$TEST_NAME.kosh" "$DRIVER_STATUS"
    rm -f "$OUTPUT"
    if [ "$TEST_STATUS" -eq 0 ]; then
      TEST_STATUS=$DRIVER_STATUS
    fi
    continue
  fi

  if [ "$REFILL_MODE" = yes ]; then
    mv "$OUTPUT" "expected/$TEST_NAME.out"
    printf "\t%-64s %s.out\n" "$TEST_NAME.kosh" "$TEST_NAME"
    continue
  fi

  if diff $DIFF_FLAGS "expected/$TEST_NAME.out" "$OUTPUT" >/dev/null 2>&1 || \
    { [ -f "expected/${TEST_NAME}_1.out" ] && \
      diff $DIFF_FLAGS "expected/${TEST_NAME}_1.out" "$OUTPUT" >/dev/null 2>&1; }; then
    printf "\t%-64s ok\033[K\r" "$TEST_NAME.kosh"
  else
    set_golden_failure_file "kosh-$TEST_NAME"
    diff $DIFF_FLAGS "expected/$TEST_NAME.out" "$OUTPUT" | \
      tee -a "$GOLDEN_FAILURE_FILE"
    printf "\t%-64s FAILED :c\n" "$TEST_NAME.kosh"
    if [ "$TEST_STATUS" -eq 0 ]; then
      TEST_STATUS=1
    fi
  fi
  rm -f "$OUTPUT"
done

exit "$TEST_STATUS"
