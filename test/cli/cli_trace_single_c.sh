unset SHIT_FLAGS
out=$("$BIN" -c no_such_command_xyz 2>&1)
rc=$?
printf 'traces=%s\n' "$(printf '%s\n' "$out" | grep -Ec 'trace( location)?:')"
printf '%s\n' "$out" | grep -q 'no_such_command_xyz' && echo "error=ok"
echo "rc=$rc"
