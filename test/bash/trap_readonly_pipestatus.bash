#!/bin/bash
# A trap action that marks PIPESTATUS read only, checked byte-for-byte against
# bash. The shell saves PIPESTATUS around every trap action and puts it back
# afterwards, and the restore has to survive a read-only mark the action left
# behind.

echo publish-under-readonly
( readonly PIPESTATUS
  false | true | false
  echo "ps=${PIPESTATUS[*]}"
  echo "rc=$?" )
echo after-publish-under-readonly=$?

echo debug-action-marks-readonly
( true | true
  trap 'readonly PIPESTATUS; echo dbg-action' DEBUG
  echo dbg-after
  trap - DEBUG )
echo after-debug-action-marks-readonly=$?

echo err-action-marks-readonly
( trap 'readonly PIPESTATUS; echo err-action' ERR
  false | false
  echo err-after
  trap - ERR )
echo after-err-action-marks-readonly=$?

echo return-action-marks-readonly
( takes_return() {
    trap 'readonly PIPESTATUS; echo ret-action' RETURN
    true | true
  }
  takes_return
  echo ret-after )
echo after-return-action-marks-readonly=$?

echo signal-action-marks-readonly
( trap 'readonly PIPESTATUS; echo usr-action' USR1
  true | false
  kill -USR1 $BASHPID
  echo usr-after )
echo after-signal-action-marks-readonly=$?

echo subshell-exit-action-marks-readonly
( trap 'readonly PIPESTATUS; echo sub-exit-action' EXIT
  false | true
  echo sub-exit-before )
echo after-subshell-exit-action-marks-readonly=$?

echo readonly-then-more-pipelines
( readonly PIPESTATUS
  true | true | true
  echo "ps-two=${PIPESTATUS[*]}"
  false
  echo "ps-three=${PIPESTATUS[*]}"
  echo "rc=$?" )
echo after-readonly-then-more-pipelines=$?

echo trap-readonly-pipestatus-done
