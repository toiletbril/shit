# The human-readable -h on du and ls, the verbose -v on cp and mv, and the
# located error a utility renders in the bash mood the same as in the default
# mood. The input file has a fixed size so the human form is the same on every
# machine.
unset SHIT_FLAGS
# A fixed umask keeps the rendered file mode the same on every machine.
umask 022
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
d=$(mktemp -d) || exit 1
cd "$d" || exit 1
"$BIN" -c 'shitbox seq 1 500 > big.txt'

echo "--- du -s prints bytes ---"
"$BIN" -c 'shitbox du -s big.txt'
echo "--- du -sh is human-readable ---"
"$BIN" -c 'shitbox du -sh big.txt'
# The owner, the group, and the time vary by machine, so the golden keeps only
# the mode, the link count, the size, and the name of the long row.
echo "--- ls -l prints bytes ---"
"$BIN" -c 'shitbox ls -l big.txt' | awk '{print $1, $2, $5, $NF}'
echo "--- ls -lh is human-readable ---"
"$BIN" -c 'shitbox ls -lh big.txt' | awk '{print $1, $2, $5, $NF}'
echo "--- cp -v names the copy ---"
"$BIN" -c 'shitbox cp -v big.txt copy.txt'
echo "--- mv -v names the move ---"
"$BIN" -c 'shitbox mv -v copy.txt moved.txt'
echo "--- utility error is located in the bash mood ---"
"$BIN" --mood bash -c 'shitbox cp' 2>&1
echo "--- and a missing operand is located in the bash mood ---"
"$BIN" --mood bash -c 'shitbox ls /no/such/path' 2>&1
