unset SHIT_FLAGS
# A function runs in the mood it was defined in. A function defined in bash mood
# expands an unset variable to empty even after the session switches to the
# strict default, and a function defined in the default mood stays strict even
# when it is called from bash mood.
echo "== bash-defined function stays lax after switching to shit:"
"$BIN" -c 'set --mood bash; f() { echo "[${UNSET}]"; }; set --mood shit; f; echo ok'
echo "== shit-defined function stays strict when called from bash mood:"
"$BIN" -c 'f() { echo "[${UNSET}]"; }; set --mood bash; f' 2>&1 | grep -o "is not set" | head -1
echo "rc-done"
