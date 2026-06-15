# calc evaluates arithmetic through the shell evaluator. The default mood computes
# in 128 bits, so 2 ** 100 prints in full, while the bash mood keeps the 64-bit
# wrap. A malformed expression renders a located caret.
unset SHIT_FLAGS

echo "=== precedence ==="
"$BIN" -c 'calc "2 + 3 * 4"'

echo "=== parentheses and power ==="
"$BIN" -c 'calc "(1 + 2) ** 3"'

echo "=== 128-bit in the default mood ==="
"$BIN" -c 'calc "2 ** 100"'

echo "=== 64-bit wrap in the bash mood ==="
"$BIN" --mood bash -c 'calc "2 ** 100"'

echo "=== variable read ==="
"$BIN" -c 'x=6; calc "x * 7"'

echo "=== located parse error ==="
"$BIN" -c 'calc "1 +"' 2>&1

echo "=== located division by zero ==="
"$BIN" -c 'calc "5 / 0"' 2>&1

echo "=== no expression ==="
"$BIN" -c 'calc' 2>&1
echo "rc=$?"
