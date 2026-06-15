# A binary reached through a shitbox utility name acts as that utility, the
# busybox multicall. The shell binary is symlinked to ls in a temporary
# directory, and running that symlink lists the directory.
unset SHIT_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
d=$(mktemp -d) || exit 1
cd "$d" || exit 1

"$BIN" -c 'shitbox seq 1 > one.txt'
"$BIN" -c 'shitbox seq 1 > two.txt'
ln -s "$BIN" ls

echo "--- binary named ls acts as ls ---"
./ls
echo "rc=$?"
