# An assignment word completes the path after its equals sign, both in command
# position and as an ordinary operand. The append form and a subscripted name
# split on the same equals sign. A command word without an equals sign still
# completes against the command sets.
d=$(mktemp -d)
cd "$d" || exit 1
mkdir -p koshdir
: > koshfile.txt
echo "== bare assignment:"
"$BIN" --debug-complete-at 'FOO=kosh' </dev/null 2>/dev/null
echo "== assignment as operand:"
"$BIN" --debug-complete-at 'echo FOO=kosh' </dev/null 2>/dev/null
echo "== append assignment:"
"$BIN" --debug-complete-at 'FOO+=kosh' </dev/null 2>/dev/null
echo "== subscripted assignment:"
"$BIN" --debug-complete-at 'FOO[1]=kosh' </dev/null 2>/dev/null
echo "== command position without an equals sign:"
"$BIN" --debug-complete-at 'koshk' </dev/null 2>/dev/null
cd / || exit 1
rm -rf "$d"
