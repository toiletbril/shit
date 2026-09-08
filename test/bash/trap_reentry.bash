#!/bin/bash

echo signal-action-debug
set -T
trap 'echo "S-[$BASH_COMMAND]"; echo s-tail' USR1
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
kill -USR1 $$
echo after-signal
trap - DEBUG
trap - USR1
set +T
echo after-signal-action-debug=$?

echo drain-inside-debug-action
trap 'echo "S-in"; echo "S-out"' USR1
trap 'echo "D-[$BASH_COMMAND]"; kill -USR1 $$; echo "D-after"' DEBUG
echo target
trap - DEBUG
trap - USR1
echo after-drain-inside-debug-action=$?

echo signal-action-no-reentry
resend_count=0
trap 'echo "R-$resend_count"; if [ $resend_count -lt 1 ]; then resend_count=$((resend_count + 1)); kill -USR2 $$; fi; echo "r-tail-$resend_count"' USR2
kill -USR2 $$
trap - USR2
echo after-signal-action-no-reentry=$?

echo signal-action-return
signal_return_target() {
  echo before-signal
  kill -USR1 $$
  echo after-signal
  return 3
}
trap 'echo in-usr1-action; return 5' USR1
signal_return_target
echo signal-return-status=$?
trap - USR1
echo after-signal-action-return=$?

echo signal-action-return-top
trap 'echo in-usr2-action; return 7' USR2
kill -USR2 $$
echo top-return-status=$?
trap - USR2
echo after-signal-action-return-top=$?

echo signal-action-break
trap 'echo in-break-action; break' USR1
for word in one two three; do
  echo loop-$word
  kill -USR1 $$
  echo after-loop-kill
done
echo break-status=$?
trap - USR1
echo after-signal-action-break=$?

echo exit-action-return
exit_return_probe() {
  trap 'echo in-exit-action; return 4' EXIT
  echo in-exit-probe
}
( exit_return_probe; echo after-exit-probe=$? )
echo after-exit-action-return=$?

echo done
