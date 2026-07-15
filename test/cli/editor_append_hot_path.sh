d=$(mktemp -d)
trap 'test -n "$d" && /bin/rm -rf "$d"' EXIT
script_command=$(command -v script)

send_input()
{
    for character in e c h o ' ' h e l l o; do
        printf %s "$character"
        sleep 0.03
    done
    printf '\nexit\n'
}

if "$script_command" --version >/dev/null 2>&1; then
    send_input | SHIT_TEST_EDITOR_STATS=1 SHIT_HISTORY="$d/history" \
        BIN="$BIN" "$script_command" -q -c \
        '/bin/stty cols 80 rows 24; exec "$BIN" -i --rcfile /dev/null' \
        "$d/typescript" >/dev/null 2>&1
else
    send_input | SHIT_TEST_EDITOR_STATS=1 SHIT_HISTORY="$d/history" \
        BIN="$BIN" "$script_command" -q "$d/typescript" /bin/sh -c \
        '/bin/stty cols 80 rows 24; exec "$BIN" -i --rcfile /dev/null' \
        >/dev/null 2>&1
fi

strings "$d/typescript" | grep -q \
    'editor-refresh append=10 full=1 metrics=1 cwd=0 stats=0 probes=0 scans=0 materialized=0' || exit 1
echo 'single-row typing uses incremental refresh'

printf '#!/bin/sh\n' > "$d/probe-alpha"
printf '#!/bin/sh\n' > "$d/probe-beta"
chmod +x "$d/probe-alpha" "$d/probe-beta"
send_path_input()
{
    for character in p r o b e; do
        printf %s "$character"
        sleep 0.03
    done
    printf '\nexit\n'
}
if "$script_command" --version >/dev/null 2>&1; then
    send_path_input | PATH="$d" SHIT_TEST_EDITOR_STATS=1 \
        SHIT_HISTORY="$d/path-history" BIN="$BIN" "$script_command" -q -c \
        '/bin/stty cols 80 rows 24; exec "$BIN" -i --rcfile /dev/null' \
        "$d/path-typescript" >/dev/null 2>&1
else
    send_path_input | PATH="$d" SHIT_TEST_EDITOR_STATS=1 \
        SHIT_HISTORY="$d/path-history" BIN="$BIN" "$script_command" -q \
        "$d/path-typescript" /bin/sh -c \
        '/bin/stty cols 80 rows 24; exec "$BIN" -i --rcfile /dev/null' \
        >/dev/null 2>&1
fi
path_metrics=$(strings "$d/path-typescript" | grep 'editor-refresh append=5 ') ||
    exit 1
path_stats=${path_metrics#* stats=}
path_stats=${path_stats%% *}
path_probes=${path_metrics#* probes=}
path_probes=${path_probes%% *}
path_scans=${path_metrics#* scans=}
path_scans=${path_scans%% *}
test "$path_stats" -le 1 || exit 1
test "$path_probes" -le 2 || exit 1
test "$path_scans" -le 64 || exit 1
case $path_metrics in *' materialized=0'*) ;; *) exit 1 ;; esac
echo 'PATH ghost completion stays bounded while typing'
