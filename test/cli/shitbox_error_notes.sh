# The shitbox usage errors carry a how-to-fix note under the located message.
# Each utility is reached through `shitbox <name>` so a binary on PATH does not
# shadow the bundled one.
unset SHIT_FLAGS

echo "=== killall arg count ==="
"$BIN" -c 'shitbox killall a b' 2>&1

echo "=== seq zero increment ==="
"$BIN" -c 'shitbox seq 1 0 10' 2>&1

echo "=== ln without -s ==="
"$BIN" -c 'shitbox ln a b' 2>&1

echo "=== tr one set ==="
"$BIN" -c 'shitbox tr abc' 2>&1

echo "=== find bad -type ==="
"$BIN" -c 'shitbox find . -type x' 2>&1
