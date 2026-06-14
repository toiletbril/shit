unset SHIT_FLAGS
# set --mood and its short -M switch the runtime mood and reseed the strictness,
# and -L is the short for --init-moods, matching the command-line flags so a
# config can set either form.
echo "== bash mood relaxes nounset:"
"$BIN" -c 'set --mood bash; echo "[${UNSETA}]"; echo ok'
echo "== -M short form prints the active mood:"
"$BIN" -c 'set -M sh; set --mood'
echo "== default mood is strict:"
"$BIN" -c 'echo "[${UNSETB}]"' 2>&1 | grep -o "is not set" | head -1
echo "== switching back to shit restores strictness:"
"$BIN" -c 'set --mood bash; set --mood shit; echo "[${UNSETC}]"' 2>&1 | grep -o "is not set" | head -1
echo "rc-done"
