#!/bin/bash
# A CHLD action that returns from the function it fired inside, checked against
# bash. The function reports the status the action asked for, and the commands
# after the triggering one are left unrun. Bash holds the CHLD action after
# such a return, so this file ends here.
report() {
  /bin/echo inner > /dev/null
  echo "report_body"
}

trap 'echo returning; return 4' CHLD
report
echo "after_report=$?"
trap - CHLD
echo done
