unset SHIT_FLAGS
directory=$(mktemp -d)
trap '[ -n "$directory" ] && /bin/rm -rf "$directory"' EXIT
plain=$directory/plain
: > "$plain"

echo "== file-type primaries follow the platform:"
if "$BIN" -c '[[ -c /dev/null ]]' </dev/null; then
    fifo=$directory/fifo
    mkfifo "$fifo"
    "$BIN" -c "[[ -p '$fifo' && ! -p '$plain' ]]" </dev/null || exit 1
    "$BIN" -c "[[ -c /dev/null && ! -c '$plain' ]]" </dev/null || exit 1
    "$BIN" -c "[[ ! -b '$plain' && ! -S '$plain' ]]" </dev/null || exit 1
    "$BIN" -c "[[ -O '$plain' ]]" </dev/null || exit 1
    "$BIN" -c "[ -p '$fifo' ] && [ ! -u '$plain' ] && [ -O '$plain' ]" \
        </dev/null || exit 1
else
    "$BIN" -c "[[ ! -p '$plain' && ! -c '$plain' ]]" </dev/null || exit 1
    "$BIN" -c "[[ ! -b '$plain' && ! -S '$plain' ]]" </dev/null || exit 1
    "$BIN" -c "[[ ! -O '$plain' ]]" </dev/null || exit 1
    "$BIN" -c "[ ! -p '$plain' ] && [ ! -u '$plain' ] && [ ! -O '$plain' ]" \
        </dev/null || exit 1
fi
echo file-types-ok

echo "== -o reads the real state of a shell option:"
"$BIN" -c "set -o pipefail; [[ -o pipefail ]] && echo pipefail-on || echo pipefail-off" </dev/null
"$BIN" -c "set +o pipefail; [[ -o pipefail ]] && echo pipefail-on || echo pipefail-off" </dev/null
"$BIN" -c "[[ -o emacs ]] && echo emacs-on || echo emacs-off" </dev/null
"$BIN" -c "[[ -o no_such_option_xyz ]] && echo unknown-on || echo unknown-off" </dev/null
