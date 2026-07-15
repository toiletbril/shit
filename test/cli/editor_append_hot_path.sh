d=$(mktemp -d)
trap 'test -n "$d" && /bin/rm -rf "$d"' EXIT

send_input()
{
    for character in e c h o ' ' h e l l o; do
        printf %s "$character"
        sleep 0.03
    done
    printf '\nexit\n'
}

if script --version >/dev/null 2>&1; then
    send_input | SHIT_TEST_EDITOR_STATS=1 SHIT_HISTORY="$d/history" \
        BIN="$BIN" script -q -c \
        'stty cols 80 rows 24; exec "$BIN" -i --rcfile /dev/null' \
        "$d/typescript" >/dev/null 2>&1
else
    send_input | SHIT_TEST_EDITOR_STATS=1 SHIT_HISTORY="$d/history" \
        BIN="$BIN" script -q "$d/typescript" /bin/sh -c \
        'stty cols 80 rows 24; exec "$BIN" -i --rcfile /dev/null' \
        >/dev/null 2>&1
fi

strings "$d/typescript" | grep -q \
    'editor-refresh append=10 full=1 metrics=1 cwd=0 stats=1 scans=[1-9][0-9]* materialized=0'
echo 'single-row typing uses incremental refresh'
