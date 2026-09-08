unset KOSH_FLAGS
# break and continue with no enclosing loop report under the bash mood, stay
# silent under the posix mood, and never carry a jump out of the construct that
# holds them.
echo "== the bash mood names the builtin and keeps running:"
"$BIN" --mood bash -c 'echo a; break; echo b; continue; echo c'; echo "rc=$?"
echo "== the status of each report is zero:"
"$BIN" --mood bash -c 'break; echo break_rc=$?; continue; echo continue_rc=$?'
echo "== a count argument does not change the report:"
"$BIN" --mood bash -c 'break 2; continue 3'; echo "rc=$?"
echo "== the posix mood stays silent:"
"$BIN" --mood sh -c 'echo a; break; echo b; continue; echo c'; echo "rc=$?"
echo "== the dropped jump does not leave a function:"
"$BIN" --mood bash -c 'f() { break; echo after_break; }; f; echo tail'
echo "== the dropped jump does not leave a subshell:"
"$BIN" --mood bash -c '( continue; echo after_continue ); echo tail'
echo "== an enclosing loop still takes the jump:"
"$BIN" --mood bash -c 'for v in 1 2 3; do echo $v; break; done; echo done'
