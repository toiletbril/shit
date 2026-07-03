unset SHIT_FLAGS
echo "== a long flag matches its whole name, not a prefix:"
"$BIN" --helpXYZ -c 'echo unreached' 2>&1 | head -1
echo "== false ignores --help and returns 1:"
"$BIN" -c 'false --help; echo "rc=$?"'
echo "== exit masks an out-of-range status to 8 bits in a subshell:"
"$BIN" -c '(exit 300); echo "$?"' 2>/dev/null
echo "== unset of an array element is not glob-expanded under failglob:"
"$BIN" -c 'a=(x y z); unset a[1]; echo "${a[@]}"'
echo "== a re-read arithmetic variable is not folded stale after a side effect:"
"$BIN" -c 'n=5; echo $((n++)) $((n)) $((n))'
