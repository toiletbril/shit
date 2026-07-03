# The rm shitbox utility. POLICY: every rm invocation in an rm test uses
# --dry-run, so the test verifies what rm would remove without ever deleting a
# real file. The dangling-symlink case proves rm sees a broken link, since it
# would remove it rather than report it absent. The guards refuse '.' and the
# root before any removal, and the dry run prints in child-before-parent order.
unset SHIT_FLAGS
d=$(mktemp -d)
cd "$d" || exit 1
echo "== rm sees a dangling symlink, so it would remove it, not report it absent:"
ln -s /no/such/target link
"$BIN" -c 'shitbox rm --dry-run link' 2>&1
echo "== --dry-run prints without deleting a file:"
: > keep.txt
"$BIN" -c 'shitbox rm --dry-run keep.txt' 2>&1
[ -e keep.txt ] && echo "keep.txt still present"
echo "== --dry-run -r lists a tree in removal order without deleting it:"
mkdir -p tree/sub
: > tree/a
: > tree/sub/b
"$BIN" -c 'shitbox rm -r --dry-run tree' 2>&1
[ -e tree ] && echo "tree still present"
echo "== rm refuses '.' and the root directory, before any removal:"
"$BIN" -c 'shitbox rm --dry-run .' 2>&1
"$BIN" -c 'shitbox rm --dry-run /' 2>&1
cd / || exit 1
[ -n "$d" ] && rm -rf "$d"
