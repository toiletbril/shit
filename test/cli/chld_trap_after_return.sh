unset KOSH_FLAGS
# A CHLD action that returns from the function it fired inside leaves the
# condition installed. Bash holds the action for the rest of the shell after
# such a return, and this file records that kosh keeps firing instead.
echo "== the action fires again after a return jump:"
"$BIN" --no-traces --mood bash -c 'chld_count=0
report() {
  "$1" --no-traces -c "exit 0"
  echo unreached
}
trap "chld_count=\$((chld_count+1)); echo fired; return 4" CHLD
report "$1"
echo "after_first=$? count=$chld_count"
report "$1"
echo "after_second=$? count=$chld_count"
trap - CHLD
"$1" --no-traces -c "exit 0"
echo "after_removal=$chld_count"' shell "$BIN"
echo "rc=$?"

echo "== the listing still holds the action after a return jump:"
"$BIN" --no-traces --mood bash -c 'report() {
  "$1" --no-traces -c "exit 0"
}
trap "return 4" CHLD
report "$1"
trap -p CHLD' shell "$BIN"
echo "rc=$?"

echo "== a child reaped outside a function still fires the action:"
"$BIN" --no-traces --mood bash -c 'chld_count=0
report() {
  "$1" --no-traces -c "exit 0"
}
trap "chld_count=\$((chld_count+1)); return 4" CHLD
report "$1"
"$1" --no-traces -c "exit 0"
echo "top_level_count=$chld_count"' shell "$BIN" 2>&1
echo "rc=$?"
