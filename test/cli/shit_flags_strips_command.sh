unset SHIT_FLAGS
# A -c in SHIT_FLAGS is dropped during the splice so the environment cannot
# inject a command into every invocation, while a benign flag there survives
# and a real command-line -c still runs.
echo "== -c in SHIT_FLAGS does not run its command:"
SHIT_FLAGS='-c injected_command' "$BIN" -c 'echo clean' 2>&1
echo "== benign flag in SHIT_FLAGS survives:"
SHIT_FLAGS='--mood sh' "$BIN" -c 'echo flagged' 2>&1
echo "rc=$?"
