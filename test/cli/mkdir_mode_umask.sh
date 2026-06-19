unset SHIT_FLAGS
# mkdir -m sets the named directory to the exact mode the way POSIX requires,
# so a bit the umask would clear is still set. A hermetic temp directory keeps
# it stable, and it is left in place so the test never runs rm.
dir=$(mktemp -d)
echo "== -m 755 under umask 077 keeps every bit (want 755):"
(umask 077; "$BIN" -c "shitbox mkdir -m 755 '$dir/exact'") </dev/null
stat -c '%a' "$dir/exact"
echo "== -p -m 777 names the deep directory exactly, parents stay umask-narrowed:"
(umask 022; "$BIN" -c "shitbox mkdir -p -m 777 '$dir/a/b/named'") </dev/null
echo -n "named (want 777): "; stat -c '%a' "$dir/a/b/named"
echo -n "parent (want 755): "; stat -c '%a' "$dir/a"
