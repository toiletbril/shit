# The find utility walks a fixed temporary tree, so the relative paths it prints
# stay the same on every machine. The children of a directory are listed in
# sorted order, so the whole walk is deterministic.
unset SHIT_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
d=$(mktemp -d) || exit 1
cd "$d" || exit 1

"$BIN" -c 'shitbox mkdir -p a/b/c'
"$BIN" -c 'shitbox touch a/one.txt'
"$BIN" -c 'shitbox touch a/b/two.log'
"$BIN" -c 'shitbox touch a/b/c/three.txt'

echo "--- find all ---"
"$BIN" -c 'shitbox find .'
echo "--- find -name *.txt ---"
"$BIN" -c 'shitbox find . -name "*.txt"'
echo "--- find -type d ---"
"$BIN" -c 'shitbox find . -type d'
echo "--- find -maxdepth 1 ---"
"$BIN" -c 'shitbox find . -maxdepth 1'
echo "--- find -mindepth 3 -type f ---"
"$BIN" -c 'shitbox find . -mindepth 3 -type f'
echo "--- find a named root ---"
"$BIN" -c 'shitbox find a/b'
echo "--- find unknown predicate ---"
"$BIN" -c 'shitbox find . -bogus' 2>&1
echo "--- find missing -name argument ---"
"$BIN" -c 'shitbox find . -name' 2>&1
