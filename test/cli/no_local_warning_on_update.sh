unset SHIT_FLAGS
# The no-local warning fires for a fresh name assigned in a function body, the
# leaking footgun, but stays quiet for a name already set at the top level or
# inherited from the environment, since that assignment updates an existing
# variable rather than leaking a new one.
echo "== fresh name warns:"
"$BIN" -W -c 'fn(){ brandnewname=1; }; fn' 2>&1 | grep -c 'has no local'
echo "== top-level name is quiet:"
"$BIN" -W -c 'seeded=0; fn(){ seeded=1; }; fn' 2>&1 | grep -c 'has no local'
echo "== inherited PATH is quiet:"
"$BIN" -W -c 'fn(){ PATH="${PATH:+$PATH:}/x"; }; fn' 2>&1 | grep -c 'has no local'
echo "rc=$?"
