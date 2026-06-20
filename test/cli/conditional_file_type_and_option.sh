unset SHIT_FLAGS
# The [[ ]] file-type primaries test the real stat type rather than mere
# existence, and -o reads a shell option from the same table the set builtin
# uses. A hermetic temp directory keeps the fifo and plain-file paths stable and
# is left in place so the test never runs rm.
dir=$(mktemp -d)
mkfifo "$dir/fifo"
: > "$dir/plain"

echo "== a fifo is -p, a plain file is not:"
"$BIN" -c "[[ -p '$dir/fifo' ]] && echo fifo-yes || echo fifo-no" </dev/null
"$BIN" -c "[[ -p '$dir/plain' ]] && echo plain-is-fifo || echo plain-not-fifo" </dev/null

echo "== /dev/null is a character device, a plain file is not:"
"$BIN" -c "[[ -c /dev/null ]] && echo char-yes || echo char-no" </dev/null
"$BIN" -c "[[ -c '$dir/plain' ]] && echo plain-is-char || echo plain-not-char" </dev/null

echo "== a plain file is neither a block device nor a socket:"
"$BIN" -c "[[ -b '$dir/plain' ]] && echo blk-yes || echo blk-no" </dev/null
"$BIN" -c "[[ -S '$dir/plain' ]] && echo sock-yes || echo sock-no" </dev/null

echo "== -O reports the file is owned by the effective user:"
"$BIN" -c "[[ -O '$dir/plain' ]] && echo owned || echo not-owned" </dev/null

echo "== -o reads the real state of a shell option:"
"$BIN" -c "set -o pipefail; [[ -o pipefail ]] && echo pipefail-on || echo pipefail-off" </dev/null
"$BIN" -c "set +o pipefail; [[ -o pipefail ]] && echo pipefail-on || echo pipefail-off" </dev/null
"$BIN" -c "[[ -o emacs ]] && echo emacs-on || echo emacs-off" </dev/null
"$BIN" -c "[[ -o no_such_option_xyz ]] && echo unknown-on || echo unknown-off" </dev/null

echo "== the [ ] builtin agrees with [[ ]] on the file-type primaries:"
"$BIN" -c "[ -p '$dir/fifo' ] && echo bracket-fifo-yes || echo bracket-fifo-no" </dev/null
"$BIN" -c "[ -u '$dir/plain' ] && echo bracket-setuid-yes || echo bracket-setuid-no" </dev/null
"$BIN" -c "[ -O '$dir/plain' ] && echo bracket-owned || echo bracket-not-owned" </dev/null
