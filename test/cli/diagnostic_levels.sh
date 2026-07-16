temporary_directory=$(mktemp -d)
trap 'test -n "$temporary_directory" && /bin/rm -rf "$temporary_directory"' EXIT
script_command=$(command -v script)

echo '== command flag ordering:'
"$BIN" -c -W 'echo level-one-after-command'
"$BIN" -c -WW 'echo level-two-after-command'

echo '== set diagnostic levels:'
"$BIN" -M bash -c '
if [[ ! -o force-warnings ]] && [[ ! -o force-diagnostics ]]; then
    echo default-level
fi
set -o force-warnings
set -o force-warnings
if [[ -o force-warnings ]] && [[ ! -o force-diagnostics ]]; then
    echo named-level-one
fi
set -o force-diagnostics
if [[ ! -o force-warnings ]] && [[ -o force-diagnostics ]]; then
    case $- in *WW*) echo named-level-two;; esac
fi
set +o force-diagnostics
if [[ ! -o force-warnings ]] && [[ ! -o force-diagnostics ]]; then
    echo disabled-level-two
fi
set -WW
if [[ ! -o force-warnings ]] && [[ -o force-diagnostics ]]; then
    echo short-level-two
fi
set +W
if [[ ! -o force-warnings ]] && [[ ! -o force-diagnostics ]]; then
    echo disabled-short-level
fi
'

"$BIN" -W -c 'echo [abc' >/dev/null 2>&1
echo "level-one-strict=$?"
"$BIN" -WW -c 'echo [abc' >/dev/null 2>&1
echo "level-two-strict=$?"

send_runtime_input()
{
    sleep 0.1
    printf '%s\n' 'set -o force-diagnostics'
    sleep 0.1
    printf '%s\n' 'echo "[$LATER_DIAGNOSTIC]"; LATER_DIAGNOSTIC=x'
    sleep 0.1
    printf '%s\n' 'exit'
}
if "$script_command" --version >/dev/null 2>&1; then
    send_runtime_input | TERM=xterm-256color \
        SHIT_HISTORY="$temporary_directory/history" BIN="$BIN" \
        "$script_command" -q -c \
        '/bin/stty cols 100 rows 24; exec "$BIN" -M bash -i --rcfile /dev/null' \
        "$temporary_directory/typescript" >/dev/null 2>&1
else
    send_runtime_input | TERM=xterm-256color \
        SHIT_HISTORY="$temporary_directory/history" BIN="$BIN" \
        "$script_command" -q "$temporary_directory/typescript" /bin/sh -c \
        '/bin/stty cols 100 rows 24; exec "$BIN" -M bash -i --rcfile /dev/null' \
        >/dev/null 2>&1
fi
runtime_warning_count=$(strings "$temporary_directory/typescript" | \
    grep -c "is read before it is assigned")
echo "runtime-level-two-warnings=$runtime_warning_count"
