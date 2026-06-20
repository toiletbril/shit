unset SHIT_FLAGS
# The calc REPL binds a name=value line as a deferred formula, the right side
# stored unevaluated so it recomputes from the variables it names on each read. A
# standalone assignment prints nothing, an unset variable is an error rather than
# a silent zero, a == comparison stays an expression, and an assignment used
# mid-line is an expression that returns its value.
echo "== a standalone assignment is silent, the read prints the value:"
printf 'x = 2 + 3\nx\nx * 2\n' | "$BIN" -c 'calc -i' 2>&1
echo "== a formula binds before its inputs and recomputes lazily:"
printf 'area = w * h\nw = 3\nh = 4\narea\nw = 10\narea\n' | "$BIN" -c 'calc -i' 2>&1
echo "== an assignment is an expression mid-line, returning its value:"
printf '(n = 5) + 1\nn\n' | "$BIN" -c 'calc -i' 2>&1
echo "== a == comparison is not an assignment:"
printf '5 == 5\n5 == 4\n' | "$BIN" -c 'calc -i' 2>&1
echo "== an unset variable is a located capitalized error, not a silent zero:"
printf 'nope\n' | "$BIN" -c 'calc -i' 2>&1
echo "== a set but empty variable reads zero, only an unset name is an error:"
printf 'e\n' | "$BIN" -c 'e=; calc -i' 2>&1
echo "== an empty assignment is rejected rather than binding an empty value:"
printf 'z =\n' | "$BIN" -c 'calc -i' 2>&1
echo "== a division by zero is a located capitalized error:"
printf '1 / 0\n' | "$BIN" -c 'calc -i' 2>&1
echo "== an unset name inside a stored formula errors when the formula reads:"
printf 'total = base + 1\ntotal\n' | "$BIN" -c 'calc -i' 2>&1
