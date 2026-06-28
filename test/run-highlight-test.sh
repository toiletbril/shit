#!/bin/bash
# Run each named highlight test through the debug highlight driver and diff the
# output against the golden. The driver is gated behind a debug build, so a
# release binary skips the whole set loudly. The Makefile passes BIN, DIFF_FLAGS,
# FAILED_LIST and the highlight test files as arguments.

if ! "$BIN" --debug-highlight-at '' </dev/null >/dev/null 2>&1; then
    for f in "$@"; do
        name=$(basename "$f" .sh)
        printf "\t%-64s skipped, release binary\n" "highlight/$name.sh"
    done
    exit 0
fi

for f in "$@"; do
    name=$(basename "$f" .sh)
    out=$(mktemp)
    BIN=$BIN sh "$f" > "$out" 2>/dev/null
    if diff $DIFF_FLAGS "expected/highlight/$name.out" "$out" >/dev/null 2>&1; then
        printf "\t%-64s ok\033[K\r" "highlight/$name.sh"
    else
        diff $DIFF_FLAGS "expected/highlight/$name.out" "$out" >> "$FAILED_LIST"
        printf "\t%-64s FAILED :c\n" "highlight/$name.sh"
    fi
    rm -f "$out"
done
