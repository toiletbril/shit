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
    send_input | TERM=xterm-256color SHIT_TEST_EDITOR_STATS=1 \
        SHIT_HISTORY="$d/history" \
        BIN="$BIN" "$script_command" -q -c \
        '/bin/stty cols 80 rows 24; exec "$BIN" -i --rcfile /dev/null' \
        "$d/typescript" >/dev/null 2>&1
else
    send_input | TERM=xterm-256color SHIT_TEST_EDITOR_STATS=1 \
        SHIT_HISTORY="$d/history" \
        BIN="$BIN" "$script_command" -q "$d/typescript" /bin/sh -c \
        '/bin/stty cols 80 rows 24; exec "$BIN" -i --rcfile /dev/null' \
        >/dev/null 2>&1
fi

append_metrics=$(strings "$d/typescript" | \
    grep 'editor-refresh append=10 ') || exit 1
append_serializations=${append_metrics#* serializations=}
append_serializations=${append_serializations%% *}
test "$append_serializations" -le 1 || exit 1
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
path_serializations=${path_metrics#* serializations=}
path_serializations=${path_serializations%% *}
test "$path_stats" -le 1 || exit 1
test "$path_probes" -le 2 || exit 1
test "$path_scans" -le 64 || exit 1
test "$path_serializations" -le 6 || exit 1
case $path_metrics in *' materialized=0'*) ;; *) exit 1 ;; esac
echo 'PATH ghost completion stays bounded while typing'

history_index=0
while [ "$history_index" -lt 1000 ]; do
    printf 'alpha-history-%s\n' "$history_index"
    history_index=$((history_index + 1))
done > "$d/miss-history"
send_missing_history_input()
{
    for character in z z z z z z z; do
        printf %s "$character"
        sleep 0.03
    done
    printf '\nexit\n'
}
if "$script_command" --version >/dev/null 2>&1; then
    send_missing_history_input | TERM=xterm-256color PATH="$d" \
        SHIT_TEST_EDITOR_STATS=1 \
        SHIT_HISTORY="$d/miss-history" BIN="$BIN" "$script_command" -q -c \
        '/bin/stty cols 80 rows 24; exec "$BIN" -i --rcfile /dev/null' \
        "$d/history-typescript" >/dev/null 2>&1
else
    send_missing_history_input | TERM=xterm-256color PATH="$d" \
        SHIT_TEST_EDITOR_STATS=1 \
        SHIT_HISTORY="$d/miss-history" BIN="$BIN" "$script_command" -q \
        "$d/history-typescript" /bin/sh -c \
        '/bin/stty cols 80 rows 24; exec "$BIN" -i --rcfile /dev/null' \
        >/dev/null 2>&1
fi
history_metrics=$(strings "$d/history-typescript" | \
    grep 'editor-refresh append=7 ') || exit 1
case $history_metrics in *' history-scans=1000 '*) ;; *) exit 1 ;; esac
echo 'missing history prefixes scan once'

mkdir "$d/absolute-path" "$d/next-directory"
printf '#!/bin/sh\n' > "$d/absolute-path/echo-probe"
chmod +x "$d/absolute-path/echo-probe"
send_cd_input()
{
    printf 'cd %s\n' "$d/next-directory"
    sleep 0.1
    printf 'e\n'
    sleep 0.1
    printf 'exit\n'
}
if "$script_command" --version >/dev/null 2>&1; then
    send_cd_input | PATH="$d/absolute-path" SHIT_TEST_EDITOR_STATS=1 \
        SHIT_HISTORY="$d/cd-history" BIN="$BIN" "$script_command" -q -c \
        '/bin/stty cols 80 rows 24; exec "$BIN" -i --rcfile /dev/null' \
        "$d/cd-typescript" >/dev/null 2>&1
else
    send_cd_input | PATH="$d/absolute-path" SHIT_TEST_EDITOR_STATS=1 \
        SHIT_HISTORY="$d/cd-history" BIN="$BIN" "$script_command" -q \
        "$d/cd-typescript" /bin/sh -c \
        '/bin/stty cols 80 rows 24; exec "$BIN" -i --rcfile /dev/null' \
        >/dev/null 2>&1
fi
cd_metrics=$(strings "$d/cd-typescript" | \
    grep 'editor-refresh append=1 ') || exit 1
case $cd_metrics in *' stats=0 probes=0 '*) ;; *) exit 1 ;; esac
echo 'absolute PATH survives a directory change'

printf '#!/bin/sh\nprintf "actual-cwd-completion\\n"\n' > \
    "$d/actual-cwd-probe"
chmod +x "$d/actual-cwd-probe"
send_clobbered_pwd_input()
{
    printf 'PWD=312312321\n'
    sleep 0.1
    printf './actual-cwd-\t\n'
    sleep 0.1
    printf 'exit\n'
}
if "$script_command" --version >/dev/null 2>&1; then
    send_clobbered_pwd_input | TEST_CWD="$d" SHIT_HISTORY="$d/pwd-history" \
        BIN="$BIN" "$script_command" -q -c \
        '/bin/stty cols 80 rows 24; cd "$TEST_CWD"; exec "$BIN" -i --rcfile /dev/null' \
        "$d/pwd-typescript" >/dev/null 2>&1
else
    send_clobbered_pwd_input | TEST_CWD="$d" SHIT_HISTORY="$d/pwd-history" \
        BIN="$BIN" "$script_command" -q "$d/pwd-typescript" /bin/sh -c \
        '/bin/stty cols 80 rows 24; cd "$TEST_CWD"; exec "$BIN" -i --rcfile /dev/null' \
        >/dev/null 2>&1
fi
strings "$d/pwd-typescript" | grep -q actual-cwd-completion || exit 1
echo 'clobbered PWD completion uses the actual directory'
