#!/bin/bash

# Every fixture drives the editor through a pseudo terminal. The native Windows
# build of Python does not provide one.
SKIP_REASON=
if ! command -v python3 >/dev/null 2>&1; then
  SKIP_REASON="python3 unavailable"
elif ! python3 -c 'import pty' >/dev/null 2>&1; then
  SKIP_REASON="python3 has no pty module"
fi

if [ -n "$SKIP_REASON" ]; then
  for TEST_FILE in "$@"; do
    printf "\t%-64s skipped, %s\n" "$TEST_FILE" "$SKIP_REASON"
  done
  exit 0
fi

OUTPUT_DIRECTORY="$TEST_TEMP_DIRECTORY/results/interactive"
mkdir -p "$OUTPUT_DIRECTORY"
TEST_STATUS=0

for TEST_FILE in "$@"; do
  if [ "$TEST_FILE" = interactive/long_warning_window.py ] && \
    [ "${IS_NONDEBUG_BUILD:-0}" = 1 ]; then
    printf "\t%-64s skipped, release binary\n" "$TEST_FILE"
    continue
  fi

  OUTPUT="$OUTPUT_DIRECTORY/$(basename "$TEST_FILE").out"
  if python3 "$TEST_FILE" "$BIN" > "$OUTPUT" 2>&1; then
    printf "\t%-64s ok\033[K\r" "$TEST_FILE"
  else
    cat "$OUTPUT"
    printf "\t%-64s FAILED :c\n" "$TEST_FILE"
    if [ "$TEST_STATUS" -eq 0 ]; then
      TEST_STATUS=1
    fi
  fi
  rm -f "$OUTPUT"
done

exit "$TEST_STATUS"
