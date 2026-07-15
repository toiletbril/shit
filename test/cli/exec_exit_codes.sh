unset SHIT_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
# exec exits 127 when the named command is not found, whether the name carries a
# slash or is searched on the PATH, exits 126 when the file is present but not
# executable, replaces the shell with a found command, and runs as a contained
# child inside a command substitution rather than killing the session.
echo "== a missing command with a slash exits 127:"
"$BIN" -c 'exec /nonexistent/cmd'; echo "rc=$?"
echo "== a missing command on the PATH exits 127:"
"$BIN" -c 'exec nonexistent_cmd_xyz'; echo "rc=$?"
echo "== a non-executable file exits 126:"
d=$(mktemp -d); printf 'data\n' > "$d/notexec"; chmod -x "$d/notexec"
real_d=$(CDPATH= cd -- "$d" && pwd -P)
out=$(cd "$d" && "$BIN" -c 'exec ./notexec' 2>&1); rc=$?
echo "$out" | sed "s#$real_d#.#g"; echo "rc=$rc"
[ -n "$d" ] && /bin/rm -rf "$d"
echo "== exec replaces the shell and runs the command:"
"$BIN" -c 'exec echo replaced'
echo "== exec in a command substitution runs as a child:"
"$BIN" -c 'echo "[$(exec echo sub)]"'
