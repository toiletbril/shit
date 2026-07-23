unset SHIT_FLAGS
out=$("$BIN" -c 'echo hi' -c no_such_command_xyz 2>&1)
rc=$?
trace_line=$(printf '%s\n' "$out" | grep 'trace:' -A1 | tail -1 | sed "s|$BIN|SHIT|")
printf '%s\n' "$trace_line"
printf '%s\n' "$out" | grep -q -- "-c 'echo hi' -c no_such_command_xyz" && echo "quoted_and_second_c=ok"
echo "rc=$rc"
