unset SHIT_FLAGS
# A bracket with no closing bracket is not a glob in the bash mood, so it stays
# literal with no directory scan. The ~+ prefix expands to PWD the way bash reads
# it.
# The test changes directory, so the relative binary path is resolved first.
BIN=$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")
d=$(mktemp -d); cd "$d" || exit 1
echo "== an unclosed bracket stays literal in the bash mood:"; "$BIN" --mood bash -c 'echo a[b'
echo "== tilde plus expands to PWD:"; "$BIN" -c 'echo ~+' | sed "s#$d#TMPDIR#"
cd /; rm -rf "$d"
