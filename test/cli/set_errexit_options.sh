unset SHIT_FLAGS
# set -e aborts the run on the first failing command, set +e turns it back off,
# an unknown --mood value is rejected, and set -o with no name lists the option
# states.
echo "== set -e aborts before the next command:"
"$BIN" -c 'set -e; false; echo unreached'; echo "rc=$?"
echo "== set +e lets the run continue:"
"$BIN" -c 'set -e; set +e; false; echo reached'; echo "rc=$?"
echo "== an unknown mood is rejected:"
"$BIN" -c 'set --mood badmood'; echo "rc=$?"
echo "== set -o lists the option states:"
"$BIN" -c 'set -o'
