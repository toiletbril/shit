# calc evaluates arithmetic through the shell evaluator and computes in 128 bits,
# so 2 ** 100 prints in full. A malformed expression renders a located caret.
# calc is a shitbox utility, so the bundled evaluator is reached through
# `shitbox calc`, which a calc binary on PATH does not shadow.
unset SHIT_FLAGS

echo "=== precedence ==="
"$BIN" -c 'shitbox calc "2 + 3 * 4"'

echo "=== parentheses and power ==="
"$BIN" -c 'shitbox calc "(1 + 2) ** 3"'

echo "=== 128-bit in the default mood ==="
"$BIN" -c 'shitbox calc "2 ** 100"'

echo "=== variable read ==="
"$BIN" -c 'x=6; shitbox calc "x * 7"'

echo "=== located parse error ==="
"$BIN" -c 'shitbox calc "1 +"' 2>&1

echo "=== located division by zero ==="
"$BIN" -c 'shitbox calc "5 / 0"' 2>&1

echo "=== no expression ==="
"$BIN" -c 'shitbox calc' 2>&1
echo "rc=$?"
