unset SHIT_FLAGS

directory=$(mktemp -d)
trap 'test -n "$directory" && /bin/rm -rf "$directory"' EXIT

echo "== standard option state:"
"$BIN" -M bash -c '
set -E -T -B -h -k
[[ -o errtrace ]] && echo errtrace-on
[[ -o functrace ]] && echo functrace-on
[[ -o braceexpand ]] && echo braceexpand-on
[[ -o hashall ]] && echo hashall-on
[[ -o keyword ]] && echo keyword-on
'

echo "== brace expansion follows its option:"
"$BIN" -M bash -c "set +B; printf '<%s>\n' {a,b}; set -B; printf '<%s>\n' {c,d}"

echo "== vi and emacs can both be off:"
"$BIN" -M bash -c '
set -o vi
set +o vi
[[ -o vi ]] || echo vi-off
[[ -o emacs ]] || echo emacs-off
'

echo "== command substitution isolates option state:"
"$BIN" -M bash -c 'set +b; value=$(set -b); set -o | grep "^notify"'

echo "== runtime noexec stops later commands:"
noexec_output=$("$BIN" -M bash -c 'set -n; echo leaked')
if [ -z "$noexec_output" ]; then
    echo noexec-clean
else
    echo noexec-leaked
fi
if "$BIN" -M bash -c 'if set -n; then echo leaked; fi' \
    > "$directory/nested-noexec"
then
    if [ ! -s "$directory/nested-noexec" ]; then
        echo nested-noexec-clean
    else
        echo nested-noexec-leaked
    fi
fi

echo "== function option changes survive defining-mood state:"
"$BIN" -M bash -c '
function enable_noclobber { set -C; }
set +C
set --mood shit
enable_noclobber
case $- in *C*) echo function-option-persisted;; *) echo function-option-lost;; esac
'
"$BIN" -M shit -c '
function enable_strict_options {
    set -u
    set -o pipefail
    set -o failglob
}
set --mood bash
enable_strict_options
if [[ -o nounset ]] && [[ -o pipefail ]] && [[ -o failglob ]]; then
    echo strict-markers-persisted
else
    echo strict-markers-lost
fi
'

echo "== command strings publish c in dollar dash:"
"$BIN" -M bash -c 'case $- in *c*) echo command-string;; *) echo missing;; esac'

echo "== interactive state is independent of hashall:"
send_interactive_input()
{
    sleep 0.1
    printf 'set +h\ncase $- in *i*) echo inter""active-letter;; *) :;; esac\nexit\n'
}
if script --version >/dev/null 2>&1; then
    send_interactive_input |
        BIN="$BIN" script -q -c 'exec "$BIN" -i --rcfile /dev/null' \
            "$directory/interactive" >/dev/null 2>&1
else
    send_interactive_input |
        BIN="$BIN" script -q "$directory/interactive" /bin/sh -c \
            'exec "$BIN" -i --rcfile /dev/null' >/dev/null 2>&1
fi
if strings "$directory/interactive" | grep -q '^interactive-letter$'; then
    echo interactive
else
    echo missing
fi

echo "== SHELLOPTS uses the shared option state:"
"$BIN" -M bash -c '
set -B -h
case :$SHELLOPTS: in *:braceexpand:*) echo braceexpand-listed;; esac
case :$SHELLOPTS: in *:hashall:*) echo hashall-listed;; esac
'

echo "== physical mode resolves a symlinked directory:"
/bin/mkdir -p "$directory/real/child" "$directory/logical"
ln -s "$directory/real" "$directory/link"
expected=$(cd "$directory/real" && pwd -P)
actual=$("$BIN" -M bash -c "set -P; cd '$directory/link'; pwd")
if [ "$actual" = "$expected" ]; then
    echo physical=yes
else
    echo physical=no
fi
ln -s "$directory/real/child" "$directory/logical/link"
actual=$(CDPATH="$directory/logical/link/.." "$BIN" -M bash -c \
    'set -P; cd child >/dev/null; pwd')
expected_cdpath=$(cd "$directory/real/child" && pwd -P)
if [ "$actual" = "$expected_cdpath" ]; then
    echo physical-cdpath=yes
else
    echo physical-cdpath=no
fi
actual=$("$BIN" -M bash -c "set -P; cd '$directory/logical/link/..'; pwd")
if [ "$actual" = "$expected" ]; then
    echo physical-dotdot=yes
else
    echo physical-dotdot=no
fi
