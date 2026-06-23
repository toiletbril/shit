#!/bin/bash
# Compare shit in the bash mood against bash across the bash compatibility
# scripts. A missing bash skips the target. A script with a _1.bash flaky
# alternative passes when shit matches either form. A mismatch appends a unified
# diff to the failed list. The Makefile passes BIN, BASH, DIFF_FLAGS,
# FAILED_LIST, and BASH_COMPAT_FILES. The file list avoids the name BASH_COMPAT,
# which bash reads as its own compatibility level.

if ! command -v $BASH >/dev/null 2>&1; then
    printf "\t%-64s skipped, no $BASH\n" bashdiff
    exit 0
fi

for f in $BASH_COMPAT_FILES; do
    s="$($BIN --mood bash "$f" 2>/dev/null; printf X)"; s="${s%X}"
    b="$($BASH "$f" 2>/dev/null; printf X)"; b="${b%X}"
    alt="${f%.bash}_1.bash"
    if [ "$s" = "$b" ]; then
        printf "\t%-64s ok\r" "$f"
    elif [ -f "$alt" ] && b1="$($BASH "$alt" 2>/dev/null; printf X)" && [ "$s" = "${b1%X}" ]; then
        printf "\t%-64s ok (flaky alternative)\n" "$f"
    else
        diff $DIFF_FLAGS --label "$f (shit)" --label "$f (bash)" \
            <(printf '%s' "$s") <(printf '%s' "$b") >> "$FAILED_LIST"
        printf "\t%-64s FAILED :c\n" "$f"
    fi
done
