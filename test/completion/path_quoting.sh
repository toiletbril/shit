# A filesystem completion whose match carries a special byte is wrapped in
# single quotes rather than backslash-escaped, while a plain name stays bare. A
# hermetic temp directory keeps the candidates stable across machines. The typed
# prefix is one token, the special byte lives in the matched entry.
case "$BIN" in /*) ;; *) BIN=$(pwd)/$BIN ;; esac
dir=$(mktemp -d)
: > "$dir/spacey file.txt"
: > "$dir/dollar\$x.txt"
: > "$dir/plainfile.txt"
cd "$dir"
echo "== a space in the match quotes it:"
"$BIN" --debug-complete-at 'cat spacey' </dev/null
echo "== a plain match stays unquoted:"
"$BIN" --debug-complete-at 'cat plain' </dev/null
echo "== a dollar in the match quotes it:"
"$BIN" --debug-complete-at 'cat dollar' </dev/null
echo "== inside a single quote the space match completes bare within it:"
"$BIN" --debug-complete-at "cat 'spacey" </dev/null
echo "== inside a double quote the space match completes bare within it:"
"$BIN" --debug-complete-at 'cat "spacey' </dev/null
