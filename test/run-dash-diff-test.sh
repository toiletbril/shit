#!/bin/bash
# Compare shit in the sh mood against dash across the sh compatibility scripts.
# A missing dash skips the target. A script with a _1.sh flaky alternative passes
# when shit matches either form. A mismatch appends a unified diff to the failed
# list. The Makefile passes BIN, DASH, DIFF_FLAGS, FAILED_LIST, and SH_COMPAT.

if ! command -v $DASH >/dev/null 2>&1; then
    printf "\t%-64s skipped, no $DASH\n" dashdiff
    exit 0
fi

for f in $SH_COMPAT; do
    s="$($BIN --mood sh "$f" 2>/dev/null; printf X)"; s="${s%X}"
    d="$($DASH "$f" 2>/dev/null; printf X)"; d="${d%X}"
    alt="${f%.sh}_1.sh"
    if [ "$s" = "$d" ]; then
        printf "\t%-64s ok\033[K\r" "$f"
    elif [ -f "$alt" ] && d1="$($DASH "$alt" 2>/dev/null; printf X)" && [ "$s" = "${d1%X}" ]; then
        printf "\t%-64s ok (flaky alternative)\n" "$f"
    else
        diff $DIFF_FLAGS --label "$f (shit)" --label "$f (dash)" \
            <(printf '%s' "$s") <(printf '%s' "$d") >> "$FAILED_LIST"
        printf "\t%-64s FAILED :c\n" "$f"
    fi
done
