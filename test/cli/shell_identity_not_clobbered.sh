unset SHIT_FLAGS
# SHELL is owned by login, getty, or the display manager, so shit leaves an
# inherited value untouched the way bash never reassigns it, and seeds its own
# invocation path only when SHELL is unset. BASH names the invocation path on
# its own, the symlink spelling such as a bash symlink to shit. The output is
# filtered to the markers so a sourced profile cannot perturb it.
echo "== an inherited SHELL is preserved, not clobbered:"
SHELL=sentinel-shell "$BIN" -c 'case "$SHELL" in sentinel-shell) echo preserved ;; *) echo "clobbered to $SHELL" ;; esac'

echo "== an unset SHELL is seeded with the invocation path:"
(unset SHELL
 "$BIN" -c 'if [ "$SHELL" = "$0" ]; then echo seeded-invocation; else echo "seeded $SHELL not $0"; fi')

dir=$(mktemp -d)
if [ "${OS-}" = Windows_NT ]; then
    "$BIN" -c 'shitbox cp "$1" "$2"' test-copy "$BIN" "$dir/bash.exe"
    bash_invocation=$dir/bash.exe
    bash_pattern='*bash.exe'
else
    ln -s "$BIN" "$dir/bash"
    bash_invocation=$dir/bash
    bash_pattern='*/bash'
fi
echo "== BASH names the invocation symlink while SHELL stays inherited:"
SHELL=sentinel-shell BASH_PATTERN="$bash_pattern" "$bash_invocation" -c \
    'case "$BASH" in $BASH_PATTERN) echo bash-is-invocation ;; *) echo "BASH=$BASH" ;; esac; case "$SHELL" in sentinel-shell) echo shell-preserved ;; *) echo "SHELL=$SHELL" ;; esac' \
    2>/dev/null
rm -rf "$dir"
