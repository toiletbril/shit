#!/bin/bash
# Push every bash and sh diff script through mimicry as well, so each one runs a
# second time and the shell picks its mood from the script's own shebang rather
# than an explicit flag. The script is run as a command with -I -c, which is what
# triggers mimicry, and the reference shell runs the same path so $0 and the
# positional parameters stay identical. A bash script compares against bash, an
# sh script against dash, since the POSIX mood targets dash semantics. stderr is
# dropped, so a shifted diagnostic line number does not affect the comparison.
# The Makefile passes BIN, BASH, DASH, DIFF_FLAGS, FAILED_LIST, SH_COMPAT, and
# BASH_COMPAT_FILES. The file list avoids the name BASH_COMPAT, which bash reads
# as its own compatibility level.

bash_skip_reason=""
if ! command -v $BASH >/dev/null 2>&1; then
    bash_skip_reason="no $BASH"
else
    bash_version=$($BASH -c 'printf "%s.%s" "${BASH_VERSINFO[0]}" "${BASH_VERSINFO[1]}"' 2>/dev/null)
    bash_major=${bash_version%%.*}
    bash_minor=${bash_version#*.}
    if [ -z "$bash_major" ] || [ "$bash_major" -lt 5 ] || { [ "$bash_major" -eq 5 ] && [ "$bash_minor" -lt 3 ]; }; then
        bash_skip_reason="$BASH is bash ${bash_version:-unknown}, need 5.3+"
    fi
fi

if [ -z "$bash_skip_reason" ]; then
    for f in $BASH_COMPAT_FILES; do
        s="$($BIN -I -c "$f" 2>/dev/null; printf X)"; s="${s%X}"
        b="$($BASH "$f" 2>/dev/null; printf X)"; b="${b%X}"
        alt="${f%.bash}_1.bash"
        if [ "$s" = "$b" ]; then
            printf "\t%-64s mimic ok\r" "$f"
        elif [ -f "$alt" ] && b1="$($BASH "$alt" 2>/dev/null; printf X)" && [ "$s" = "${b1%X}" ]; then
            printf "\t%-64s mimic ok (flaky alternative)\n" "$f"
        else
            diff $DIFF_FLAGS --label "$f (shit -I)" --label "$f (bash)" \
                <(printf '%s' "$s") <(printf '%s' "$b") >> "$FAILED_LIST"
            printf "\t%-64s mimic FAILED :c\n" "$f"
        fi
    done
else
    printf "\t%-64s skipped, %s\n" mimicrydiff "$bash_skip_reason"
fi

if command -v $DASH >/dev/null 2>&1; then
    for f in $SH_COMPAT; do
        s="$($BIN -I -c "$f" 2>/dev/null; printf X)"; s="${s%X}"
        d="$($DASH "$f" 2>/dev/null; printf X)"; d="${d%X}"
        alt="${f%.sh}_1.sh"
        if [ "$s" = "$d" ]; then
            printf "\t%-64s mimic ok\r" "$f"
        elif [ -f "$alt" ] && d1="$($DASH "$alt" 2>/dev/null; printf X)" && [ "$s" = "${d1%X}" ]; then
            printf "\t%-64s mimic ok (flaky alternative)\n" "$f"
        else
            diff $DIFF_FLAGS --label "$f (shit -I)" --label "$f (dash)" \
                <(printf '%s' "$s") <(printf '%s' "$d") >> "$FAILED_LIST"
            printf "\t%-64s mimic FAILED :c\n" "$f"
        fi
    done
else
    printf "\t%-64s skipped, no $DASH\n" mimicrydiff
fi
