unset SHIT_FLAGS
# A login spawn, marked by a dash-prefixed argv[0], drops to the rescue prompt
# when a flag fails to parse rather than exiting, so a broken login config does
# not lock the user out. A non-login invocation with the same bad flag exits 2
# without a rescue prompt. The login shell is built through a dash-prefixed
# symlink the way login and tmux start one.
dir=$(mktemp -d)
case "$BIN" in /*) ;; *) BIN=$(pwd)/$BIN ;; esac
ln -s "$BIN" "$dir/-shit"
echo "== login shell with a bad flag enters rescue:"
PATH="$dir:$PATH" "-shit" --nonexistent-flag </dev/null 2>&1 | grep -c "Entering rescue"
echo "== non-login shell with the same bad flag does not:"
"$BIN" --nonexistent-flag </dev/null 2>&1 | grep -c "Entering rescue"
rm -rf "$dir"
