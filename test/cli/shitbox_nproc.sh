unset SHIT_FLAGS

default_count=$(PATH= "$BIN" -c 'set -o shitbox; nproc')
case "$default_count" in
    *[!0-9]*|'') echo default-invalid ;;
    0) echo default-zero ;;
    *) echo default-positive ;;
esac

if [ "$(uname -s)" = Linux ] && command -v taskset >/dev/null 2>&1; then
    affinity_list=$(taskset -pc $$)
    affinity_list=${affinity_list##*: }
    first_processor=${affinity_list%%[-,]*}
    affinity_count=$(taskset -c "$first_processor" "$BIN" -c \
        'shitbox nproc')
    [ "$affinity_count" -eq 1 ] || exit 1
fi

all_count=$(
    PATH= "$BIN" -c 'set -o shitbox; nproc --all'
)
case "$all_count" in
    *[!0-9]*|'') echo all-invalid ;;
    0) echo all-zero ;;
    *)
        if [ "$all_count" -ge "$default_count" ]; then
            echo all-positive
        else
            echo all-too-small
        fi
        ;;
esac

ignored_one=$(PATH= "$BIN" -c 'set -o shitbox; nproc --ignore=1')
expected_one=$((default_count > 1 ? default_count - 1 : 1))
if [ "$ignored_one" -eq "$expected_one" ]; then
    echo ignore-one
else
    echo ignore-one-wrong
fi

ignored_zero=$(PATH= "$BIN" -c 'set -o shitbox; nproc --ignore=0')
if [ "$ignored_zero" -eq "$default_count" ]; then
    echo ignore-zero
else
    echo ignore-zero-wrong
fi

ignored_large=$(
    PATH= "$BIN" -c 'set -o shitbox; nproc --ignore=18446744073709551615'
)
if [ "$ignored_large" -eq 1 ]; then
    echo ignore-saturates
else
    echo ignore-did-not-saturate
fi

for invalid in x -1 '' 18446744073709551616; do
    if PATH= NPROC_INVALID=$invalid "$BIN" -c \
        'set -o shitbox; nproc --ignore="$NPROC_INVALID"' >/dev/null 2>&1
    then
        echo invalid-accepted
    else
        echo invalid-rejected
    fi
done

if PATH= "$BIN" -c 'set -o shitbox; nproc operand' >/dev/null 2>&1; then
    echo operand-accepted
else
    echo operand-rejected
fi

help=$(PATH= "$BIN" -c 'set -o shitbox; nproc --help')
case "$help" in
    *--all*--ignore*) echo help-flags ;;
    *) echo help-missing ;;
esac

listed=$(PATH= "$BIN" -c 'shitbox --list' | /usr/bin/grep -c '^nproc$')
if [ "$listed" -eq 1 ]; then
    echo listed-once
else
    echo listed-wrong
fi
