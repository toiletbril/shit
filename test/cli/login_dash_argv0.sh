unset SHIT_FLAGS
# A login spawn hands the shell a dash-prefixed argv[0], the way tmux and
# login start -bash, so the name is never read as a flag bundle and the
# session comes up as a login shell instead of dying on Unknown flag '-b'.
dir=$(mktemp -d)
case "$BIN" in /*) ;; *) BIN=$(pwd)/$BIN ;; esac
ln -s "$BIN" "$dir/-shit"
PATH="$dir:$PATH" "-shit" -c 'echo login_spawn_ok' 2>/dev/null | grep -c login_spawn_ok
rm -rf "$dir"
echo "rc=$?"
# exec -l prepends a dash to the whole argv[0], so a path invocation such as
# -/usr/local/bin/shit still marks a login shell. The dash is stripped from
# $SHIT while $0 keeps the dashed spelling.
out=$( ( exec -l "$BIN" -c 'printf "%s\n" "$SHIT"' ) 2>/dev/null )
case "$out" in
  -*) echo "shle_leading_dash=yes" ;;
  *)  echo "shle_leading_dash=no" ;;
esac
