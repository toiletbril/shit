unset SHIT_FLAGS
# find rejects a negative -maxdepth or -mindepth rather than reading it as the
# unlimited sentinel, so a typo'd negative depth errors instead of walking the
# whole tree unbounded.
echo "== -maxdepth -1 is rejected:"
"$BIN" -c "shitbox find . -maxdepth -1" </dev/null 2>&1 | grep -c "non-negative number"
echo "== -mindepth -3 is rejected:"
"$BIN" -c "shitbox find . -mindepth -3" </dev/null 2>&1 | grep -c "non-negative number"
echo "== a valid -maxdepth 0 still works (lists only the root):"
"$BIN" -c "shitbox find . -maxdepth 0" </dev/null
