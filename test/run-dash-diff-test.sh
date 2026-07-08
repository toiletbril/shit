#!/bin/bash
# Compare shit in the sh mood against dash across the sh compatibility scripts.
# A missing dash skips the target. A script with a _1.sh flaky alternative passes
# when shit matches either form. A mismatch appends a unified diff to the failed
# list. The Makefile passes BIN, DASH, DIFF_FLAGS, FAILED_LIST, and SH_COMPAT.

if ! command -v $DASH >/dev/null 2>&1; then
    printf "\t%-64s skipped, no $DASH\n" dashdiff
    exit 0
fi

diff_one() {
    local file=$1
    local s d alt d1
    s="$($BIN --mood sh "$file" 2>/dev/null; printf X)"; s="${s%X}"
    d="$($DASH "$file" 2>/dev/null; printf X)"; d="${d%X}"
    alt="${file%.sh}_1.sh"
    if [ "$s" = "$d" ]; then
        printf "\t%-64s ok\033[K\r" "$file"
    elif [ -f "$alt" ] && d1="$($DASH "$alt" 2>/dev/null; printf X)" && [ "$s" = "${d1%X}" ]; then
        printf "\t%-64s ok (flaky alternative)\n" "$file"
    else
        diff $DIFF_FLAGS --label "$file (shit)" --label "$file (dash)" \
            <(printf '%s' "$s") <(printf '%s' "$d") >> "$FAILED_LIST"
        printf "\t%-64s FAILED :c\n" "$file"
    fi
}

for f in $SH_COMPAT; do
    diff_one "$f"
done
