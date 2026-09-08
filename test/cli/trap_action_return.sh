unset KOSH_FLAGS
# A return a DEBUG action requests belongs to the enclosing function or sourced
# file. The traced command is abandoned and the scope returns the action status.
echo "== a return in an action leaves the function with its status:"
"$BIN" --mood bash -c 'set -T
f() {
  trap '"'"'case "$BASH_COMMAND" in echo\ inner*) return 7;; esac'"'"' DEBUG
  echo inner-1
  echo inner-2
  trap - DEBUG
  echo unreached
}
f
echo f-rc=$?
echo tail'; echo "rc=$?"
echo "== a return in an action abandons the loop around the traced command:"
"$BIN" --mood bash -c 'set -T
f() {
  trap '"'"'case "$BASH_COMMAND" in echo\ inner*) return 9;; esac'"'"' DEBUG
  for v in 1 2 3; do echo inner-$v; done
  trap - DEBUG
  echo unreached
}
f
echo f-rc=$?
echo tail'; echo "rc=$?"
echo "== a return in an action leaves the sourced file with its status:"
d=$(mktemp -d) || exit 1
trap '[ -n "$d" ] && /bin/rm -rf "$d"' EXIT
printf 'echo sourced-1\necho sourced-2\n' > "$d/sourced.sh"
"$BIN" --mood bash -c 'set -T
trap '"'"'case "$BASH_COMMAND" in echo\ sourced*) return 3;; esac'"'"' DEBUG
. '"$d"'/sourced.sh
echo src-rc=$?
trap - DEBUG
echo tail' 2>&1 | sed "s|$d|DIR|g" | ./normalize-trace.sh "$BIN"; echo "rc=${PIPESTATUS[0]}"
echo "== a return with no function and no sourced file runs the traced command:"
"$BIN" --mood bash -c 'set -T
trap '"'"'case "$BASH_COMMAND" in echo\ top*) return 5;; esac'"'"' DEBUG
echo top-1
echo top-2
trap - DEBUG
echo tail' 2>&1 | ./normalize-trace.sh "$BIN"; echo "rc=${PIPESTATUS[0]}"
