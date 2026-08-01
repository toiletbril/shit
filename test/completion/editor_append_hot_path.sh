d=$(mktemp -d)
trap 'test -n "$d" && /bin/rm -rf "$d"' EXIT
script_command=$(command -v script)
RCFILE="$d/editor-rc"
export RCFILE
printf 'printf ready > "$EDITOR_READY_FILE"\n' > "$RCFILE"

if "$script_command" --version >/dev/null 2>&1; then
    script_style=gnu
else
    script_style=bsd
fi

run_editor()
{
    transcript=$1
    columns=${2-80}
    script_error=$transcript.script-error
    if [ "$script_style" = gnu ]; then
        "$script_command" -q -c \
            "/bin/stty cols $columns rows 24; exec \"\$BIN\" -i --rcfile \"\$RCFILE\"" \
            "$transcript" >/dev/null 2>"$script_error" || :
    else
        "$script_command" -q -F "$transcript" /bin/sh -c \
            "/bin/stty cols $columns rows 24; exec \"\$BIN\" -i --rcfile \"\$RCFILE\"" \
            >/dev/null 2>"$script_error" || :
    fi

    return 0
}

wait_for_editor()
{
    ready_file=$1
    attempt_count=0
    while ! grep -F ready "$ready_file" >/dev/null 2>&1; do
        [ "$attempt_count" -lt 1000 ] || return 1
        sleep 0.01
        attempt_count=$((attempt_count + 1))
    done
}

metric_line()
{
    local metric_line_value
    metric_line_value=$(strings "$1" | grep 'editor-refresh append=' |
        sed -n "${2}p")
    [ -n "$metric_line_value" ] || return 1
    printf '%s\n' "$metric_line_value"
}

metric_field()
{
    metric=$1
    field=$2
    value=${metric#* "$field"=}
    printf '%s\n' "${value%% *}"
}

mkdir "$d/path"
printf '#!/bin/sh\n' > "$d/path/probe-alpha"
printf '#!/bin/sh\n' > "$d/path/probe-beta"
chmod +x "$d/path/probe-alpha" "$d/path/probe-beta"

send_typing_input()
{
    wait_for_editor "$d/typing-ready" || exit 1
    for character in e c h o ' ' h e l l o; do
        printf %s "$character"
        sleep 0.02
    done
    printf '\n'
    for character in p r o b e; do
        printf %s "$character"
        sleep 0.02
    done
    printf '\nexit\n'
}

send_typing_input | TERM=xterm-256color PATH="$d/path" \
    SHIT_TEST_EDITOR_STATS=1 EDITOR_READY_FILE="$d/typing-ready" \
    SHIT_HISTORY="$d/typing-history" BIN="$BIN" \
    run_editor "$d/typing-typescript"

append_metrics=$(metric_line "$d/typing-typescript" 1) || {
    printf 'editor metrics missing\n'
    strings "$d/typing-typescript" || true
    [ ! -s "$d/typing-typescript.script-error" ] ||
        /bin/cat "$d/typing-typescript.script-error"
    exit 1
}
append_refreshes=$(metric_field "$append_metrics" append)
full_refreshes=$(metric_field "$append_metrics" full)
append_serializations=$(metric_field "$append_metrics" serializations)
test "$((append_refreshes + full_refreshes))" -eq 11 || exit 1
test "$append_refreshes" -ge 7 || exit 1
test "$append_serializations" -le 1 || exit 1
echo 'single-row typing uses incremental refresh'

path_metrics=$(metric_line "$d/typing-typescript" 2) || exit 1
test "$(metric_field "$path_metrics" stats)" -le 1 || exit 1
test "$(metric_field "$path_metrics" probes)" -le 2 || exit 1
test "$(metric_field "$path_metrics" sorts)" -eq 0 || exit 1
path_scan_count=$(metric_field "$path_metrics" scans)
append_scan_count=$(metric_field "$append_metrics" scans)
test "$((path_scan_count - append_scan_count))" -le 64 || exit 1
test "$(metric_field "$path_metrics" serializations)" -le 6 || exit 1
case $path_metrics in *' materialized=0'*) ;; *) exit 1 ;; esac
echo 'PATH ghost completion stays bounded while typing'

tab_unrelated_index=0
while [ "$tab_unrelated_index" -lt 32 ]; do
    printf '#!/bin/sh\n' > "$d/path/unrelated-command-$tab_unrelated_index"
    chmod +x "$d/path/unrelated-command-$tab_unrelated_index"
    tab_unrelated_index=$((tab_unrelated_index + 1))
done

send_tab_input()
{
    wait_for_editor "$d/tab-ready" || exit 1
    printf 'probe\talpha\n'
    sleep 0.1
    printf 'compgen -c >/dev/null 2>&1; cd /\n'
    sleep 0.1
    printf 'probe\t\t\nexit\n'
}

send_tab_input | PATH="$d/path" SHIT_TEST_EDITOR_STATS=1 \
    EDITOR_READY_FILE="$d/tab-ready" SHIT_HISTORY="$d/tab-history" \
    BIN="$BIN" run_editor "$d/tab-typescript"

tab_metrics=$(metric_line "$d/tab-typescript" 1) || exit 1
test "$(metric_field "$tab_metrics" probes)" -le 4 || exit 1
echo 'TAB validation ends before the next key'

warm_metrics=$(metric_line "$d/tab-typescript" 3) || exit 1
case $warm_metrics in
    *' preprompt-stats=0 preprompt-reads=0 preprompt-sorts=0 preprompt-probes=0 preprompt-resolutions=0 preprompt-history-loads=0 '*) ;;
    *) exit 1 ;;
esac
test "$(metric_field "$warm_metrics" stats)" -eq 0 || exit 1
test "$(metric_field "$warm_metrics" reads)" -eq 0 || exit 1
test "$(metric_field "$warm_metrics" sorts)" -eq 0 || exit 1
test "$(metric_field "$warm_metrics" probes)" -eq 0 || exit 1
test "$(metric_field "$warm_metrics" resolutions)" -eq 0 || exit 1
echo 'repeated warm TAB and absolute PATH perform no PATH work after cd'

history_index=0
while [ "$history_index" -lt 32 ]; do
    printf 'zzzz-invalid-history-command-%s\n' "$history_index"
    history_index=$((history_index + 1))
done > "$d/miss-history"

send_history_input()
{
    wait_for_editor "$d/history-ready" || exit 1
    printf 'zz\nzzzzzzz\nexit\n'
}

send_history_input | TERM=xterm-256color PATH="$d/path" \
    EDITOR_READY_FILE="$d/history-ready" SHIT_TEST_EDITOR_STATS=1 \
    SHIT_HISTORY="$d/miss-history" BIN="$BIN" \
    run_editor "$d/history-typescript"

history_short_metrics=$(metric_line "$d/history-typescript" 1) || exit 1
history_metrics=$(metric_line "$d/history-typescript" 2) || exit 1
history_entry_scan_count=$(metric_field "$history_metrics" history-scans)
test "$history_entry_scan_count" -ge 32 || exit 1
test "$history_entry_scan_count" -le 33 || exit 1
case $history_metrics in *' history-loads=0 '*) ;; *) exit 1 ;; esac
history_short_scan_count=$(metric_field "$history_short_metrics" scans)
history_scan_count=$(metric_field "$history_metrics" scans)
test "$history_scan_count" -le "$history_short_scan_count" || exit 1
test "$(metric_field "$history_metrics" resolutions)" -eq 0 || exit 1
echo 'rejected history prefixes scan once without walking PATH'

mkdir "$d/startup-before" "$d/startup-after"
printf '#!/bin/sh\n' > "$d/startup-after/git"
chmod +x "$d/startup-after/git"
printf '%s\n' \
    "PS1='> '" \
    'printf ready > "$EDITOR_READY_FILE"' \
    "PROMPT_COMMAND='PATH=\"\$STARTUP_AFTER\"; unset PROMPT_COMMAND'" \
    > "$d/startup-rc"

send_startup_input()
{
    wait_for_editor "$d/startup-ready" || exit 1
    printf 'git \177z\003exit\n'
}

send_startup_input | TERM=xterm-256color NO_COLOR= PATH="$d/startup-before" \
    EDITOR_READY_FILE="$d/startup-ready" STARTUP_AFTER="$d/startup-after" \
    SHIT_TEST_EDITOR_STATS=1 SHIT_HISTORY="$d/startup-history" \
    RCFILE="$d/startup-rc" BIN="$BIN" run_editor "$d/startup-typescript"

startup_metrics=$(metric_line "$d/startup-typescript" 1) || exit 1
test "$(metric_field "$startup_metrics" stats)" -eq 0 || exit 1
test "$(metric_field "$startup_metrics" reads)" -eq 0 || exit 1
test "$(metric_field "$startup_metrics" probes)" -eq 0 || exit 1
test "$(metric_field "$startup_metrics" sorts)" -eq 0 || exit 1
test "$(metric_field "$startup_metrics" resolutions)" -le 1 || exit 1
echo 'prompt PATH changes defer work while startup commands highlight'

mkdir "$d/menu-bin"
cat > "$d/menu-bin/tailscale" <<'SH'
#!/bin/sh
printf '%s\n' \
    'SUBCOMMANDS' \
    '  alpha        Keep this first long completion description intact' \
    '  beta         Keep this second long completion description intact'
SH
chmod +x "$d/menu-bin/tailscale"

send_menu_input()
{
    wait_for_editor "$d/menu-ready" || exit 1
    printf 'tailscale \t\003exit\n'
}

send_menu_input | ASAN_OPTIONS=detect_stack_use_after_return=1 \
    EDITOR_READY_FILE="$d/menu-ready" MANPATH= \
    PATH="$d/menu-bin${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" \
    SHIT_HISTORY="$d/menu-history" BIN="$BIN" \
    run_editor "$d/menu-typescript" 100

strings "$d/menu-typescript" | \
    grep -q 'Keep this first long completion description intact' || exit 1
strings "$d/menu-typescript" | \
    grep -q 'Keep this second long completion description intact' || exit 1
echo 'completion menu keeps callback-owned strings alive'

mkdir "$d/quoted-completion"
touch "$d/quoted-completion/space name" \
    "$d/quoted-completion/plain name" \
    "$d/quoted-completion/README-one" \
    "$d/quoted-completion/ReadMe-two"

send_quoted_input()
{
    wait_for_editor "$d/quoted-ready" || exit 1
    printf "cd '%s'\nprintf '<%%s>\\\\n' 'spX'\033[D\033[D\t\n" \
        "$d/quoted-completion"
    printf "printf '<%%s>\\\\n' plaX\033[D\t\n"
    printf "COMPLETION_PROBE=variable-value\n"
    printf "printf '<%%s>\\\\n' \"\$COMPLETION_PROBX\"\033[D\033[D\t\n"
    printf "printf '<%%s>\\\\n' read\tone\nexit\n"
}

send_quoted_input | EDITOR_READY_FILE="$d/quoted-ready" \
    SHIT_HISTORY="$d/quoted-history" BIN="$BIN" \
    run_editor "$d/quoted-typescript"

strings "$d/quoted-typescript" | grep -q '<space name>' || exit 1
strings "$d/quoted-typescript" | grep -q '<plain name>' || exit 1
strings "$d/quoted-typescript" | grep -q '<variable-value>' || exit 1
strings "$d/quoted-typescript" | grep -q '<README-one>' || exit 1
echo 'quoted replacement and smart-case TAB preserve the completed token'

mkdir "$d/mixed-path" "$d/next-directory"
printf '#!/bin/sh\n' > "$d/mixed-path/after-cd-probe"
chmod +x "$d/mixed-path/after-cd-probe"

send_mixed_path_input()
{
    wait_for_editor "$d/mixed-ready" || exit 1
    printf 'cd %s\nafter-cd-probe\014\nexit\n' "$d/next-directory"
}

unset NO_COLOR
send_mixed_path_input | TERM=xterm-256color \
    EDITOR_READY_FILE="$d/mixed-ready" \
    PATH="$d/mixed-path${TEST_PATH_SEPARATOR}${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" \
    SHIT_TEST_EDITOR_STATS=1 SHIT_HISTORY="$d/mixed-history" BIN="$BIN" \
    run_editor "$d/mixed-typescript"

mixed_metrics=$(metric_line "$d/mixed-typescript" 2) || exit 1
case $mixed_metrics in *' stats=0 reads=0 sorts=0 probes=0 '*) ;; *) exit 1 ;; esac
echo 'mixed PATH keeps stale absolute commands highlighted after cd'

printf '#!/bin/sh\nprintf "actual-cwd-completion\\n"\n' > "$d/actual-cwd-probe"
chmod +x "$d/actual-cwd-probe"
actual_cwd_completion=$(
    cd "$d" && PWD=invalid "$BIN" --debug-complete-at './actual-cwd-'
)
printf '%s\n' "$actual_cwd_completion" | grep -q actual-cwd-probe || exit 1
echo 'clobbered PWD completion uses the actual directory'
