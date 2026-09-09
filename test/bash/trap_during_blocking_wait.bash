#!/bin/bash

# A trapped signal that arrives while wait blocks ends the wait. The action
# runs, wait reports 128 plus the signal number, and the job it was waiting on
# is left running. A bare wait ends the same way, and a wait with several
# operands abandons the operands it has not reached. A wait that no signal
# reaches reports the status of its child, and an ignored signal leaves the
# wait blocked.

echo wait-on-a-pid
trap 'echo action-pid' USR1
( /bin/sleep 1; kill -USR1 $$ ) &
notifier=$!
/bin/sleep 4 &
slow=$!
wait "$slow"
echo "pid-status=$?"
kill -0 "$slow" 2> /dev/null
echo "pid-child-alive=$?"
kill "$slow" 2> /dev/null
wait "$notifier" 2> /dev/null
trap - USR1
echo wait-on-a-pid-done

echo bare-wait
trap 'echo action-bare' USR1
( /bin/sleep 1; kill -USR1 $$ ) &
notifier=$!
/bin/sleep 4 &
slow=$!
wait
echo "bare-status=$?"
kill -0 "$slow" 2> /dev/null
echo "bare-child-alive=$?"
kill "$slow" 2> /dev/null
wait "$notifier" 2> /dev/null
trap - USR1
echo bare-wait-done

echo wait-with-two-operands
trap 'echo action-two' USR1
( /bin/sleep 1; kill -USR1 $$ ) &
notifier=$!
/bin/sleep 4 &
first=$!
/bin/sh -c 'exit 5' &
second=$!
wait "$first" "$second"
echo "two-status=$?"
kill "$first" 2> /dev/null
wait "$notifier" 2> /dev/null
trap - USR1
echo wait-with-two-operands-done

echo wait-that-no-signal-reaches
trap 'echo action-quiet' USR1
/bin/sh -c 'exit 7' &
quiet=$!
wait "$quiet"
echo "quiet-status=$?"
trap - USR1
echo wait-that-no-signal-reaches-done

echo wait-under-an-ignored-signal
trap '' USR1
( /bin/sleep 1; kill -USR1 $$ ) &
notifier=$!
/bin/sh -c '/bin/sleep 2; exit 6' &
ignored=$!
wait "$ignored"
echo "ignored-status=$?"
wait "$notifier" 2> /dev/null
trap - USR1
echo wait-under-an-ignored-signal-done

echo trap-during-blocking-wait-done
