#!/bin/bash
# Push every bash and sh diff script through mimicry as well, so each one runs a
# second time and the shell picks its mood from the script's own shebang rather
# than an explicit flag. The script is run as a command with -I -c, which is what
# triggers mimicry, and the reference shell runs the same path so $0 and the
# positional parameters stay identical. A bash script compares against bash, an
# sh script against dash, since the POSIX mood targets dash semantics. stderr is
# dropped, so a shifted diagnostic line number does not affect the comparison.
# The Makefile passes BIN, BASHP, DASH, DIFF_FLAGS, FAILED_LIST, SH_COMPAT, and
# BASH_COMPAT_FILES. The file list avoids the name BASH_COMPAT, which bash reads
# as its own compatibility level.

mimic_one() {
    local ref=$1 file=$2 suffix=$3 label=$4
    local s d alt r1
    s="$($BIN -I -c "$file" 2>/dev/null; printf X)"; s="${s%X}"
    d="$($ref "$file" 2>/dev/null; printf X)"; d="${d%X}"
    alt="${file%$suffix}_1$suffix"
    if [ "$s" = "$d" ]; then
        printf "\t%-64s mimic ok\033[K\r" "$file"
    elif [ -f "$alt" ] && r1="$($ref "$alt" 2>/dev/null; printf X)" && [ "$s" = "${r1%X}" ]; then
        printf "\t%-64s mimic ok (flaky alternative)\n" "$file"
    else
        diff $DIFF_FLAGS --label "$file (shit -I)" --label "$file ($label)" \
            <(printf '%s' "$s") <(printf '%s' "$d") >> "$FAILED_LIST"
        printf "\t%-64s mimic FAILED :c\n" "$file"
    fi
}

bash_skip_reason=""
if ! command -v $BASHP >/dev/null 2>&1; then
    bash_skip_reason="no $BASHP"
else
    bash_version=$($BASHP -c 'printf "%s.%s" "${BASH_VERSINFO[0]}" "${BASH_VERSINFO[1]}"' 2>/dev/null)
    bash_major=${bash_version%%.*}
    bash_minor=${bash_version#*.}
    if [ -z "$bash_major" ] || [ "$bash_major" -lt 5 ] || { [ "$bash_major" -eq 5 ] && [ "$bash_minor" -lt 3 ]; }; then
        bash_skip_reason="$BASHP is bash ${bash_version:-unknown}, need 5.3+"
    fi
fi

if [ -z "$bash_skip_reason" ]; then
    for f in $BASH_COMPAT_FILES; do
        mimic_one "$BASHP" "$f" .bash bash
    done
else
    printf "\t%-64s skipped, %s\n" mimicrydiff "$bash_skip_reason"
fi

if command -v $DASH >/dev/null 2>&1; then
    for f in $SH_COMPAT; do
        mimic_one "$DASH" "$f" .sh dash
    done
else
    printf "\t%-64s skipped, no $DASH\n" mimicrydiff
fi
