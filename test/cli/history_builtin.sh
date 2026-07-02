unset SHIT_FLAGS
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
printf 'echo one\nls\ncd /tmp\ngit status\n' > "$dir/hist"
export SHIT_HISTORY="$dir/hist"
echo "== the numbered list prints every entry =="
"$BIN" -c 'history'
echo "== a trailing count prints only the most recent entries =="
"$BIN" -c 'history 2'
echo "== a non-numeric count is rejected without printing the list =="
"$BIN" -c 'history foo; echo "rc=$?"'
echo "== a count past the list size still prints every entry, no overflow =="
"$BIN" -c 'history 999999999999999999999999'
echo "== the print flag echoes its operands and stores nothing =="
"$BIN" -c 'history -p alpha beta'
echo "== builtin history -a no longer reports an unknown builtin =="
"$BIN" -c 'builtin history -a; echo continued'
echo "== type reports the builtin =="
"$BIN" -c 'type history'
echo "== clear empties the list =="
"$BIN" -c 'history -c; history; echo cleared'
