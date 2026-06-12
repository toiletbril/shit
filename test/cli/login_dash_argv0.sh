unset SHIT_FLAGS
# A login spawn hands the shell a dash-prefixed argv[0], the way tmux and
# login start -bash, so the name is never read as a flag bundle and the
# session comes up as a login shell instead of dying on Unknown flag '-b'.
dir=$(mktemp -d)
case "$BIN" in
  /*) target="$BIN" ;;
  *) target="$(pwd)/$BIN" ;;
esac
ln -s "$target" "$dir/-shit"
PATH="$dir:$PATH" "-shit" -c 'echo login_spawn_ok' 2>/dev/null | grep -c login_spawn_ok
rm -rf "$dir"
echo "rc=$?"
