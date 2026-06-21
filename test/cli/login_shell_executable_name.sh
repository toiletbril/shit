unset SHIT_FLAGS
# A login shell receives a dash-prefixed argv[0], a bare - or -shit, the spawn
# convention login and tmux use. The dash marks the login but is not part of a
# runnable path, so SHELL and $0 drop it and name the executable a child can run.
# The login shell is built through a dash-prefixed symlink the way login starts
# one, and the output is filtered to the two markers so a sourced profile cannot
# perturb it.
dir=$(mktemp -d)
case "$BIN" in /*) ;; *) BIN=$(pwd)/$BIN ;; esac
ln -s "$BIN" "$dir/-shit"
echo "== a login shell names SHELL and \$0 by the runnable spelling:"
PATH="$dir:$PATH" "-shit" -c '
  case "$SHELL" in -*) echo "SHELL=dashed" ;; *) echo "SHELL=runnable" ;; esac
  case "$0" in -*) echo "zero=dashed" ;; *) echo "zero=runnable" ;; esac
' 2>/dev/null | grep -E "^(SHELL|zero)="
rm -rf "$dir"
