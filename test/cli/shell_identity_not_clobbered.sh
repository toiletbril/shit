unset SHIT_FLAGS
# SHELL is owned by login, getty, or the display manager, so shit leaves an
# inherited value untouched the way bash never reassigns it, and seeds its own
# invocation path only when SHELL is unset. BASH names the invocation path on
# its own, the symlink spelling such as a bash symlink to shit. The output is
# filtered to the markers so a sourced profile cannot perturb it.
case "$BIN" in /*) ;; *) BIN=$(pwd)/$BIN ;; esac

echo "== an inherited SHELL is preserved, not clobbered:"
SHELL=/sentinel/shell "$BIN" -c 'case "$SHELL" in /sentinel/shell) echo preserved ;; *) echo "clobbered to $SHELL" ;; esac'

echo "== an unset SHELL is seeded with the invocation path:"
env -u SHELL "$BIN" -c 'if [ "$SHELL" = "$0" ]; then echo seeded-invocation; else echo "seeded $SHELL not $0"; fi'

dir=$(mktemp -d)
ln -s "$BIN" "$dir/bash"
echo "== BASH names the invocation symlink while SHELL stays inherited:"
SHELL=/sentinel/shell "$dir/bash" -c 'case "$BASH" in */bash) echo bash-is-invocation ;; *) echo "BASH=$BASH" ;; esac; case "$SHELL" in /sentinel/shell) echo shell-preserved ;; *) echo "SHELL=$SHELL" ;; esac' 2>/dev/null
rm -rf "$dir"
