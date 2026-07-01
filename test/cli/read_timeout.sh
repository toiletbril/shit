echo "== read -t 0 polls, data ready reports success:"
printf 'ready\n' | "$BIN" -c 'read -r -t 0 x; echo "rc=$?"'
echo "== read -t 0 polls, EOF counts as readable:"
"$BIN" -c 'read -r -t 0 x </dev/null; echo "rc=$?"'
echo "== read -t with data present returns it:"
printf 'hello\n' | "$BIN" -c 'read -r -t 5 x; echo "rc=$? x=[$x]"'
echo "== read -t -n reads the count and stops:"
printf 'abcdef' | "$BIN" -c 'read -r -t 5 -n 3 x; echo "rc=$? x=[$x]"'
echo "== read -t times out on a silent producer, status is 142:"
"$BIN" -c 'read -r -t 0.1 x < <(sleep 2); echo "rc=$?"'
echo "== an unparsable timeout is rejected:"
"$BIN" -c 'read -r -t nope x </dev/null; echo "rc=$?"'
