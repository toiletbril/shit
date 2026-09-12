unset KOSH_FLAGS
# A trapped signal that arrives while wait blocks ends the wait in every mood.
# The action runs and wait reports 128 plus the signal number. The signal number
# of USR1 differs between platforms. Each section compares the reported status
# against a number the shell under test derives, because the host shell of a
# bounded fixture cannot name a signal. A child arrival leaves the wait blocked,
# because every external command reaps a child and a wake for each one would end
# every wait.
expected=$("$BIN" --no-traces -c 'echo $((128 + $(kill -l USR1)))')
echo "== a trapped signal ends a blocking wait in the default mood:"
"$BIN" --no-traces -c 'trap "echo action-default" USR1
( /bin/sleep 1; kill -USR1 $$ ) &
/bin/sleep 4 &
slow=$!
wait "$slow"
status=$?
if [ "$status" = "'"$expected"'" ]; then echo status=derived; else echo "status=$status"; fi
kill "$slow" 2> /dev/null
wait 2> /dev/null'; echo "rc=$?"
echo "== a trapped signal ends a blocking wait in the sh mood:"
"$BIN" --no-traces --mood sh -c 'trap "echo action-sh" USR1
( /bin/sleep 1; kill -USR1 $$ ) &
/bin/sleep 4 &
slow=$!
wait "$slow"
status=$?
if [ "$status" = "'"$expected"'" ]; then echo status=derived; else echo "status=$status"; fi
kill "$slow" 2> /dev/null
wait 2> /dev/null'; echo "rc=$?"
echo "== the interrupted wait leaves its job running:"
"$BIN" --no-traces --mood bash -c 'trap "echo action-alive" USR1
( /bin/sleep 1; kill -USR1 $$ ) &
/bin/sleep 4 &
slow=$!
wait "$slow"
kill -0 "$slow" 2> /dev/null
echo "child-alive=$?"
kill "$slow" 2> /dev/null
wait 2> /dev/null'; echo "rc=$?"
echo "== a child arrival leaves the wait blocked:"
"$BIN" --no-traces --mood bash -c 'trap "echo chld-fired" CHLD
/bin/sh -c "/bin/sleep 1; exit 4" &
/bin/sh -c "/bin/sleep 3; exit 6" &
slow=$!
wait "$slow"
echo "chld-wait-status=$?"'; echo "rc=$?"
echo "== a signal with no action leaves the wait blocked:"
"$BIN" --no-traces --mood bash -c 'trap "" USR1
( /bin/sleep 1; kill -USR1 $$ ) &
/bin/sh -c "/bin/sleep 2; exit 5" &
slow=$!
wait "$slow"
echo "ignored-wait-status=$?"
wait 2> /dev/null'; echo "rc=$?"
