unset SHIT_FLAGS
# The calc REPL accepts name=value assignments. The value is stored as text so a
# formula evaluates lazily and recomputes from the variables it names. A ==
# comparison stays an expression, and a value that is not evaluatable is
# rejected with a located error.
echo "== assignment prints the value and stores it:"
printf 'x = 2 + 3\nx\nx * 2\n' | "$BIN" -c 'calc -i' 2>&1
echo "== a stored formula recomputes lazily when a referenced var changes:"
printf 'a = 10\nb = a + 1\nb\na = 100\nb\n' | "$BIN" -c 'calc -i' 2>&1
echo "== a == comparison is not an assignment:"
printf '5 == 5\n5 == 4\n' | "$BIN" -c 'calc -i' 2>&1
echo "== a bad value is rejected and leaves the variable unset:"
printf 'z = 2 +\nz\n' | "$BIN" -c 'calc -i' 2>&1
