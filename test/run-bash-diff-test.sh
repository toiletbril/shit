#!/bin/bash
# Compare shit in the bash mood against bash across the bash compatibility
# scripts. A missing bash skips the target. A script with a _1.bash flaky
# alternative passes when shit matches either form. A mismatch appends a unified
# diff to the failed list. The Makefile passes BIN, BASHP, DIFF_FLAGS,
# FAILED_LIST, and BASH_COMPAT_FILES. The file list avoids the name BASH_COMPAT,
# which bash reads as its own compatibility level.

if ! command -v $BASHP >/dev/null 2>&1; then
    printf "\t%-64s skipped, no $BASHP\n" bashdiff
    exit 0
fi

bash_version=$($BASHP -c 'printf "%s.%s" "${BASH_VERSINFO[0]}" "${BASH_VERSINFO[1]}"' 2>/dev/null)
bash_major=${bash_version%%.*}
bash_minor=${bash_version#*.}
if [ -z "$bash_major" ] || [ "$bash_major" -lt 5 ] || { [ "$bash_major" -eq 5 ] && [ "$bash_minor" -lt 3 ]; }; then
    printf "\t%-64s skipped, %s is bash %s, need 5.3+\n" bashdiff "$BASHP" "${bash_version:-unknown}"
    exit 0
fi

diff_one() {
    local file=$1
    local s b alt b1
    s="$($BIN --mood bash "$file" 2>/dev/null; printf X)"; s="${s%X}"
    b="$($BASHP "$file" 2>/dev/null; printf X)"; b="${b%X}"
    alt="${file%.bash}_1.bash"
    if [ "$s" = "$b" ]; then
        printf "\t%-64s ok\033[K\r" "$file"
    elif [ -f "$alt" ] && b1="$($BASHP "$alt" 2>/dev/null; printf X)" && [ "$s" = "${b1%X}" ]; then
        printf "\t%-64s ok (flaky alternative)\n" "$file"
    else
        diff $DIFF_FLAGS --label "$file (shit)" --label "$file (bash)" \
            <(printf '%s' "$s") <(printf '%s' "$b") >> "$FAILED_LIST"
        printf "\t%-64s FAILED :c\n" "$file"
    fi
}

for f in $BASH_COMPAT_FILES; do
    diff_one "$f"
done
