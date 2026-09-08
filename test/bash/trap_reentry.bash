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

echo done
