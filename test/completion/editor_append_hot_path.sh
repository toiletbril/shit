d=$(mktemp -d)
trap 'test -n "$d" && /bin/rm -rf "$d"' EXIT
script_command=$(command -v script)

wait_for_editor()
{
    sleep 0.5
}

send_input()
{
    wait_for_editor
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
    grep 'editor-refresh append=' | head -1) || exit 1
append_refreshes=${append_metrics#* append=}
append_refreshes=${append_refreshes%% *}
full_refreshes=${append_metrics#* full=}
full_refreshes=${full_refreshes%% *}
append_serializations=${append_metrics#* serializations=}
append_serializations=${append_serializations%% *}
test "$((append_refreshes + full_refreshes))" -eq 11 || exit 1
test "$append_refreshes" -ge 7 || exit 1
test "$append_serializations" -le 1 || exit 1
echo 'single-row typing uses incremental refresh'

printf '#!/bin/sh\n' > "$d/probe-alpha"
printf '#!/bin/sh\n' > "$d/probe-beta"
chmod +x "$d/probe-alpha" "$d/probe-beta"
send_path_input()
{
    wait_for_editor
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
path_metrics=$(strings "$d/path-typescript" | \
    grep 'editor-refresh append=' | head -1) || exit 1
path_append_refreshes=${path_metrics#* append=}
path_append_refreshes=${path_append_refreshes%% *}
path_full_refreshes=${path_metrics#* full=}
path_full_refreshes=${path_full_refreshes%% *}
path_stats=${path_metrics#* stats=}
path_stats=${path_stats%% *}
path_probes=${path_metrics#* probes=}
path_probes=${path_probes%% *}
path_sorts=${path_metrics#* sorts=}
path_sorts=${path_sorts%% *}
path_scans=${path_metrics#* scans=}
path_scans=${path_scans%% *}
path_serializations=${path_metrics#* serializations=}
path_serializations=${path_serializations%% *}
test "$((path_append_refreshes + path_full_refreshes))" -eq 6 || exit 1
test "$path_stats" -le 1 || exit 1
test "$path_probes" -le 2 || exit 1
test "$path_sorts" -eq 0 || exit 1
test "$path_scans" -le 64 || exit 1
test "$path_serializations" -le 6 || exit 1
case $path_metrics in *' materialized=0'*) ;; *) exit 1 ;; esac
echo 'PATH ghost completion stays bounded while typing'

mkdir "$d/tab-path"
printf '#!/bin/sh\n' > "$d/tab-path/probe-alpha"
printf '#!/bin/sh\n' > "$d/tab-path/probe-beta"
chmod +x "$d/tab-path/probe-alpha" "$d/tab-path/probe-beta"
tab_unrelated_index=0
while [ "$tab_unrelated_index" -lt 256 ]; do
    printf '#!/bin/sh\n' > \
        "$d/tab-path/unrelated-command-$tab_unrelated_index"
    chmod +x "$d/tab-path/unrelated-command-$tab_unrelated_index"
    tab_unrelated_index=$((tab_unrelated_index + 1))
done
send_post_tab_input()
{
    wait_for_editor
    for character in p r o b e; do
        printf %s "$character"
        sleep 0.03
    done
    printf '\t'
    sleep 0.03
    for character in a l p h a; do
        printf %s "$character"
        sleep 0.03
    done
    printf '\nexit\n'
}
if "$script_command" --version >/dev/null 2>&1; then
    send_post_tab_input | PATH="$d/tab-path" SHIT_TEST_EDITOR_STATS=1 \
        SHIT_HISTORY="$d/tab-history" BIN="$BIN" "$script_command" -q -c \
        '/bin/stty cols 80 rows 24; exec "$BIN" -i --rcfile /dev/null' \
        "$d/tab-typescript" >/dev/null 2>&1
else
    send_post_tab_input | PATH="$d/tab-path" SHIT_TEST_EDITOR_STATS=1 \
        SHIT_HISTORY="$d/tab-history" BIN="$BIN" "$script_command" -q \
        "$d/tab-typescript" /bin/sh -c \
        '/bin/stty cols 80 rows 24; exec "$BIN" -i --rcfile /dev/null' \
        >/dev/null 2>&1
fi
tab_metrics=$(strings "$d/tab-typescript" | \
    grep 'editor-refresh append=' | head -1) || exit 1
tab_probes=${tab_metrics#* probes=}
tab_probes=${tab_probes%% *}
test "$tab_probes" -le 4 || exit 1
echo 'TAB validation ends before the next key'

send_warm_tab_input()
{
    wait_for_editor
    printf 'compgen -c >/dev/null 2>&1; cd /\n'
    sleep 0.1
    printf 'probe\t\t\n'
    sleep 0.1
    printf 'exit\n'
}
if "$script_command" --version >/dev/null 2>&1; then
    send_warm_tab_input | PATH="$d/tab-path" SHIT_TEST_EDITOR_STATS=1 \
        SHIT_HISTORY="$d/warm-tab-history" BIN="$BIN" "$script_command" -q -c \
        '/bin/stty cols 80 rows 24; exec "$BIN" -i --rcfile /dev/null' \
        "$d/warm-tab-typescript" >/dev/null 2>&1
else
    send_warm_tab_input | PATH="$d/tab-path" SHIT_TEST_EDITOR_STATS=1 \
        SHIT_HISTORY="$d/warm-tab-history" BIN="$BIN" "$script_command" -q \
        "$d/warm-tab-typescript" /bin/sh -c \
        '/bin/stty cols 80 rows 24; exec "$BIN" -i --rcfile /dev/null' \
        >/dev/null 2>&1
fi
warm_tab_metrics=$(strings "$d/warm-tab-typescript" | \
    grep 'editor-refresh append=' | tail -n +2 | head -1) || exit 1
case $warm_tab_metrics in
    *' preprompt-stats=0 preprompt-reads=0 preprompt-sorts=0 preprompt-probes=0 preprompt-resolutions=0 preprompt-history-loads=0 '*)
        ;;
    *) exit 1 ;;
esac
echo 'warm prompts perform no synchronous PATH or history work'
warm_tab_stats=${warm_tab_metrics#* stats=}
warm_tab_stats=${warm_tab_stats%% *}
warm_tab_reads=${warm_tab_metrics#* reads=}
warm_tab_reads=${warm_tab_reads%% *}
warm_tab_sorts=${warm_tab_metrics#* sorts=}
warm_tab_sorts=${warm_tab_sorts%% *}
warm_tab_probes=${warm_tab_metrics#* probes=}
warm_tab_probes=${warm_tab_probes%% *}
warm_tab_resolutions=${warm_tab_metrics#* resolutions=}
warm_tab_resolutions=${warm_tab_resolutions%% *}
test "$warm_tab_stats" -eq 0 || exit 1
test "$warm_tab_reads" -eq 0 || exit 1
test "$warm_tab_sorts" -eq 0 || exit 1
test "$warm_tab_probes" -eq 0 || exit 1
test "$warm_tab_resolutions" -eq 0 || exit 1
echo 'repeated warm TAB performs no PATH work'

history_index=0
while [ "$history_index" -lt 1000 ]; do
    printf 'zzzz-invalid-history-command-%s\n' "$history_index"
    history_index=$((history_index + 1))
done > "$d/miss-history"
send_missing_history_input()
{
    wait_for_editor
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
    grep 'editor-refresh append=' | head -1) || exit 1
history_append_refreshes=${history_metrics#* append=}
history_append_refreshes=${history_append_refreshes%% *}
history_full_refreshes=${history_metrics#* full=}
history_full_refreshes=${history_full_refreshes%% *}
test "$((history_append_refreshes + history_full_refreshes))" -eq 8 || exit 1
case $history_metrics in *' history-scans=1000 '*) ;; *) exit 1 ;; esac
echo 'rejected history prefixes scan once'
case $history_metrics in *' history-loads=0 '*) ;; *) exit 1 ;; esac
echo 'first history key reads memory only'
history_resolutions=${history_metrics#* resolutions=}
history_resolutions=${history_resolutions%% *}
test "$history_resolutions" -eq 0 || exit 1
echo 'history validation does not walk PATH'

mkdir "$d/prompt-initial" "$d/prompt-path-one" "$d/prompt-path-two" \
    "$d/prompt-path-three" "$d/prompt-path-four"
prompt_index=0
while [ "$prompt_index" -lt 256 ]; do
    printf '#!/bin/sh\n' > \
        "$d/prompt-path-one/prompt-command-$prompt_index"
    chmod +x "$d/prompt-path-one/prompt-command-$prompt_index"
    prompt_index=$((prompt_index + 1))
done
send_prompt_path_input()
{
    wait_for_editor
    printf z
    sleep 0.1
    printf '\003exit\n'
}
if "$script_command" --version >/dev/null 2>&1; then
    send_prompt_path_input | \
        PATH="$d/prompt-initial:/bin" \
        PROMPT_COMMAND="PATH=$d/prompt-path-one:$d/prompt-path-two:$d/prompt-path-three:$d/prompt-path-four" \
        SHIT_TEST_EDITOR_STATS=1 SHIT_HISTORY="$d/prompt-history" \
        BIN="$BIN" "$script_command" -q -c \
        '/bin/stty cols 80 rows 24; exec "$BIN" -i --rcfile /dev/null' \
        "$d/prompt-typescript" >/dev/null 2>&1
else
    send_prompt_path_input | \
        PATH="$d/prompt-initial:/bin" \
        PROMPT_COMMAND="PATH=$d/prompt-path-one:$d/prompt-path-two:$d/prompt-path-three:$d/prompt-path-four" \
        SHIT_TEST_EDITOR_STATS=1 SHIT_HISTORY="$d/prompt-history" \
        BIN="$BIN" "$script_command" -q "$d/prompt-typescript" /bin/sh -c \
        '/bin/stty cols 80 rows 24; exec "$BIN" -i --rcfile /dev/null' \
        >/dev/null 2>&1
fi
prompt_metrics=$(strings "$d/prompt-typescript" | \
    grep 'editor-refresh append=' | head -1) || exit 1
prompt_stats=${prompt_metrics#* stats=}
prompt_stats=${prompt_stats%% *}
prompt_reads=${prompt_metrics#* reads=}
prompt_reads=${prompt_reads%% *}
prompt_probes=${prompt_metrics#* probes=}
prompt_probes=${prompt_probes%% *}
prompt_sorts=${prompt_metrics#* sorts=}
prompt_sorts=${prompt_sorts%% *}
prompt_resolutions=${prompt_metrics#* resolutions=}
prompt_resolutions=${prompt_resolutions%% *}
test "$prompt_stats" -eq 0 || exit 1
test "$prompt_reads" -eq 0 || exit 1
test "$prompt_probes" -eq 0 || exit 1
test "$prompt_sorts" -eq 0 || exit 1
test "$prompt_resolutions" -eq 0 || exit 1
echo 'prompt PATH changes defer first-key filesystem work'

mkdir "$d/absolute-path" "$d/next-directory"
printf '#!/bin/sh\n' > "$d/absolute-path/echo-probe"
chmod +x "$d/absolute-path/echo-probe"
send_cd_input()
{
    wait_for_editor
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
    grep 'editor-refresh append=' | tail -n +2 | head -1) || exit 1
cd_append_refreshes=${cd_metrics#* append=}
cd_append_refreshes=${cd_append_refreshes%% *}
cd_full_refreshes=${cd_metrics#* full=}
cd_full_refreshes=${cd_full_refreshes%% *}
test "$((cd_append_refreshes + cd_full_refreshes))" -eq 2 || exit 1
case $cd_metrics in *' stats=0 reads=0 sorts=0 probes=0 '*) ;; *) exit 1 ;; esac
echo 'absolute PATH survives a directory change'

printf '#!/bin/sh\nprintf "actual-cwd-completion\\n"\n' > \
    "$d/actual-cwd-probe"
chmod +x "$d/actual-cwd-probe"
send_clobbered_pwd_input()
{
    wait_for_editor
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

mkdir "$d/menu-bin"
cat > "$d/menu-bin/tailscale" <<'SH'
#!/bin/sh
printf '%s\n' \
    'SUBCOMMANDS' \
    '  alpha        Keep this first long completion description intact' \
    '  beta         Keep this second long completion description intact'
SH
chmod +x "$d/menu-bin/tailscale"
send_completion_menu_input()
{
    wait_for_editor
    printf 'tailscale \t'
    sleep 0.2
    printf '\003exit\n'
}
if "$script_command" --version >/dev/null 2>&1; then
    send_completion_menu_input | \
        ASAN_OPTIONS=detect_stack_use_after_return=1 \
        MANPATH= PATH="$d/menu-bin:/bin:/usr/bin" \
        SHIT_HISTORY="$d/menu-history" BIN="$BIN" \
        "$script_command" -q -c \
        '/bin/stty cols 100 rows 24; exec "$BIN" -i --rcfile /dev/null' \
        "$d/menu-typescript" >/dev/null 2>&1
else
    send_completion_menu_input | \
        ASAN_OPTIONS=detect_stack_use_after_return=1 \
        MANPATH= PATH="$d/menu-bin:/bin:/usr/bin" \
        SHIT_HISTORY="$d/menu-history" BIN="$BIN" \
        "$script_command" -q "$d/menu-typescript" /bin/sh -c \
        '/bin/stty cols 100 rows 24; exec "$BIN" -i --rcfile /dev/null' \
        >/dev/null 2>&1
fi
strings "$d/menu-typescript" | \
    grep -q 'Keep this first long completion description intact' || exit 1
strings "$d/menu-typescript" | \
    grep -q 'Keep this second long completion description intact' || exit 1
echo 'completion menu keeps callback-owned strings alive'
