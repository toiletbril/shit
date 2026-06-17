# The shitbox file utilities run inside a fresh temporary directory so the output
# names no absolute path and stays the same on every machine. The binary path is
# resolved to an absolute one first, since the working directory changes.
unset SHIT_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
d=$(mktemp -d) || exit 1
cd "$d" || exit 1

"$BIN" -c 'shitbox mkdir -p a/b'
"$BIN" -c 'shitbox seq 3 > nums.txt'
echo "--- ls ---"
"$BIN" -c 'shitbox ls'
echo "--- ls a ---"
"$BIN" -c 'shitbox ls a'
"$BIN" -c 'shitbox cp nums.txt copy.txt'
"$BIN" -c 'shitbox mv copy.txt moved.txt'
"$BIN" -c 'shitbox touch stamp'
"$BIN" -c 'shitbox ln -s nums.txt sym'
# The owner, the group, and the time of a long row vary by machine, so only the
# mode, the link count, the size, and the name are kept for a stable golden.
echo "--- ls -l sym (mode nlink size name) ---"
"$BIN" -c 'shitbox ls -l sym' | awk '{print $1, $2, $5, $NF}'
echo "--- ls after operations ---"
"$BIN" -c 'shitbox ls'
echo "--- du -s nums.txt ---"
"$BIN" -c 'shitbox du -s nums.txt'
echo "--- basename ---"
"$BIN" -c 'shitbox basename /usr/local/libfoo.so .so'
echo "--- dirname ---"
"$BIN" -c 'shitbox dirname /usr/local/libfoo.so'
echo "--- realpath lexical ---"
"$BIN" -c 'cd /; shitbox realpath ./usr/../usr/lib'
"$BIN" -c 'shitbox rmdir a/b'
echo "--- ls a after rmdir ---"
"$BIN" -c 'shitbox ls a'
echo "--- unlink removes a single file ---"
"$BIN" -c 'shitbox touch victim; shitbox unlink victim; echo "unlink rc=$?"'
"$BIN" -c 'shitbox ls' | grep -c victim
echo "--- unlink a directory fails ---"
"$BIN" -c 'shitbox unlink a' 2>&1
echo "rc=$?"
