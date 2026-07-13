unset SHIT_FLAGS
# A single -c invocation renders the CLI trace line and underlines the -c token
# with its argument. The argument has no spaces, so it stays unquoted. The old
# version rendered no trace line at all for a -c run. The binary path the
# harness invoked varies by host and build mode, so it is normalized to SHIT
# the way warning_source_chain normalizes INNER and OUTER.
out=$("$BIN" -c no_such_command_xyz 2>&1)
rc=$?
trace_line=$(printf '%s\n' "$out" | grep 'trace location' -A1 | tail -1 | sed "s|$BIN|SHIT|")
printf '%s\n' "$trace_line"
printf '%s\n' "$out" | grep -q -- '-c no_such_command_xyz' && echo "unquoted_argv=ok"
echo "rc=$rc"
