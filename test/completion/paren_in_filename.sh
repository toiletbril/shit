# A file whose name holds a paren completes through a quote, a backslash, and
# bare, so an open paren no longer resets the completion to command position and
# drops the data file. A hermetic temp directory keeps the candidates stable.
case "$BIN" in /*) ;; *) BIN=$(pwd)/$BIN ;; esac
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
: > "$dir/report(1).log"
: > "$dir/report(2).log"
cd "$dir"
echo "== a bare paren glued to the name completes the file:"
"$BIN" --debug-complete-at 'cat report(1' </dev/null
echo "== a bare open paren keeps the filesystem context:"
"$BIN" --debug-complete-at 'cat report(' </dev/null
echo "== a backslash-escaped paren completes the file:"
"$BIN" --debug-complete-at 'cat report\(1' </dev/null
echo "== a double-quoted paren completes the file:"
"$BIN" --debug-complete-at 'cat "report(' </dev/null
echo "== a single-quoted paren completes the file:"
"$BIN" --debug-complete-at "cat 'report(" </dev/null
