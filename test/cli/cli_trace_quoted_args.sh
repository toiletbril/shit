unset SHIT_FLAGS
# The CLI trace wraps the first -c body in single quotes and underlines the
# second -c body, the one whose command failed. The old version underlined the
# first -c and left the spaced body unquoted.
out=$("$BIN" -c 'echo hi' -c no_such_command_xyz 2>&1)
rc=$?
trace_line=$(printf '%s\n' "$out" | grep 'trace location' -A1 | tail -1)
printf '%s\n' "$trace_line"
printf '%s\n' "$out" | grep -q -- "-c 'echo hi' -c no_such_command_xyz" && echo "quoted_and_second_c=ok"
echo "rc=$rc"
