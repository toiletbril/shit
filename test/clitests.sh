#!/bin/bash
# Run each named cli test through its own shell driver and diff the output
# against the golden. A mismatch appends a unified diff to the failed list. The
# Makefile passes BIN, DIFF_FLAGS, FAILED_LIST and the cli test files as
# arguments.

for f in "$@"; do
    name=$(basename "$f" .sh)
    out=$(mktemp)
    BIN=$BIN sh "$f" > "$out" 2>&1
    if diff $DIFF_FLAGS "expected/cli/$name.out" "$out" >/dev/null 2>&1; then
        printf "\t%-64s ok\r" "cli/$name.sh"
    else
        diff $DIFF_FLAGS "expected/cli/$name.out" "$out" >> "$FAILED_LIST"
        printf "\t%-64s FAILED :c\n" "cli/$name.sh"
    fi
    rm -f "$out"
done
