#!/bin/bash
# Run each named highlight test through the debug highlight driver and diff the
# output against the golden. The driver is gated behind a debug build, so a
# release binary skips the whole set loudly. The Makefile passes BIN, DIFF_FLAGS,
# FAILED_LIST, the test shell, and the highlight test files as arguments.

test_shell=$1
shift

if [ "${IS_NONDEBUG_BUILD:-0}" = 1 ]; then
    printf "\t%-64s skipped, release binary\n" highlight
    exit 0
fi

for f in "$@"; do
    name=$(basename "$f" .sh)
    output_directory="$TEST_TEMP_DIRECTORY/results/highlight"
    mkdir -p "$output_directory"
    out="$output_directory/$name.out"
    BIN="$BIN" "$test_shell" "$f" > "$out" 2>/dev/null
    if diff $DIFF_FLAGS "expected/$name.out" "$out" >/dev/null 2>&1; then
        printf "\t%-64s ok\033[K\r" "highlight/$name.sh"
    else
        diff $DIFF_FLAGS "expected/$name.out" "$out" | \
            tee -a "$FAILED_LIST"
        printf "\t%-64s FAILED :c\n" "highlight/$name.sh"
    fi
    rm -f "$out"
done
