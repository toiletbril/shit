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

echo subshell-exit-action-status
( trap 'echo S-status; exit 7' EXIT
  echo sub-body )
echo after-subshell-exit-action-status=$?

echo subshell-exit-action-clears-failure
( trap 'echo S-clear; exit 0' EXIT
  false )
echo after-subshell-exit-action-clears-failure=$?

echo subshell-exit-action-over-explicit-exit
( trap 'echo S-over; exit 9' EXIT
  exit 2 )
echo after-subshell-exit-action-over-explicit-exit=$?

echo substitution-exit-action
capture_value=$( trap 'echo S-sub; exit 3' EXIT
  echo captured )
echo "capture=[$capture_value] after-substitution-exit-action=$?"

echo nested-subshell-exit-action
( ( trap 'echo S-inner; exit 5' EXIT
    echo inner-body )
  echo after-inner=$? )
echo after-nested-subshell-exit-action=$?

echo subshell-exit-action-in-pipeline
( trap 'echo S-stage; exit 6' EXIT
  echo stage-body ) | cat
echo after-subshell-exit-action-in-pipeline=$?

echo subshell-bare-exit-keeps-entry-status
( trap 'echo S-bare; true; exit' EXIT
  false )
echo after-subshell-bare-exit-keeps-entry-status=$?

echo subshell-bare-exit-ignores-action-failure
( trap 'echo S-bare-fail; false; exit' EXIT
  true )
echo after-subshell-bare-exit-ignores-action-failure=$?

echo err-action-bare-exit
( trap 'echo E-bare; true; exit' ERR
  false
  echo unreachable-after-err-bare-exit )
echo after-err-action-bare-exit=$?

echo done
