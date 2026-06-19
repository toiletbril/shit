unset SHIT_FLAGS
# shitbox --assimilate installs a symlink to the binary named for each utility
# into a directory, the busybox-style install. A symlinked invocation routes its
# own flags to the utility rather than the shell CLI. A hermetic temp directory
# keeps it stable, and it is left in place so the test never runs rm.
dir=$(mktemp -d)
"$BIN" -c "shitbox --assimilate '$dir'" </dev/null
echo "== a symlink was installed for head:"
[ -L "$dir/head" ] && echo head-linked || echo head-missing
echo "== a symlinked head routes its own -c flag to the utility:"
printf 'abcdefgh' > "$dir/sample"
"$dir/head" -c 3 "$dir/sample"
echo ""
echo "== a symlinked ls routes -A to ls (count of the dot file):"
: > "$dir/.dotfile"
"$dir/ls" -A -1 "$dir" | grep -c '^\.dotfile$'
