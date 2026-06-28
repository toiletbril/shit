#!/bin/bash
# Run each named default test through shit and diff its output against the
# golden. A name with an _1.out alternative passes when shit matches either
# form. A mismatch appends a unified diff to the failed list. The Makefile
# passes BIN, BIN_FLAGS, DIFF_FLAGS, FAILED_LIST and the test names as
# arguments.

for name in "$@"; do
    [ -f "shit/$name.shit" ] || continue
    out=$(mktemp)
    cat "shit/$name.shit" | $BIN $BIN_FLAGS - > "$out" 2>&1
    if diff $DIFF_FLAGS "expected/$name.out" "$out" >/dev/null 2>&1 || \
       { [ -f "expected/${name}_1.out" ] && \
         diff $DIFF_FLAGS "expected/${name}_1.out" "$out" >/dev/null 2>&1; }; then
        printf "\t%-64s ok\033[K\r" "$name.shit"
    else
        diff $DIFF_FLAGS "expected/$name.out" "$out" >> "$FAILED_LIST"
        printf "\t%-64s FAILED :c\n" "$name.shit"
    fi
    rm -f "$out"
done
