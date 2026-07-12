unset SHIT_FLAGS
# A missing script file operand with spaces in its name is wrapped in single
# quotes in the trace line, and the caret lands on the quoted operand.
out=$("$BIN" "no such script file" 2>&1)
rc=$?
printf '%s\n' "$out" | grep -o "Could not open 'no such script file'"
printf '%s\n' "$out" | grep -o "'no such script file'"
echo "rc=$rc"
