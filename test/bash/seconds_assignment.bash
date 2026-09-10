#!/bin/bash

# An assignment to SECONDS moves the base its elapsed count is measured from,
# and a later read resumes counting from the assigned value. A local
# declaration turns the name into an ordinary frozen variable for the length of
# the call, and the outer count keeps running underneath it. A count read after
# a sleep is reported through a range, because the truncated second the two
# shells start their sleep in is their own.

report_range() {
  if [ "$2" -ge "$3" ] && [ "$2" -le "$4" ]; then
    echo "$1=within-$3-to-$4"
  else
    echo "$1=$2"
  fi
}

echo assigned-base
SECONDS=100
sleep 1
report_range plain "$SECONDS" 101 102
SECONDS=0
sleep 1
report_range zero "$SECONDS" 1 2
SECONDS=-5
sleep 1
report_range negative "$SECONDS" -4 -3
SECONDS=abc
sleep 1
report_range text "$SECONDS" 1 2
echo assigned-base-done

echo declared-form
SECONDS=7
declare -p SECONDS
export SECONDS=5
declare -p SECONDS
sleep 1
report_range exported "$SECONDS" 6 7
export -n SECONDS
echo declared-form-done

echo local-shadow
count_inside() {
  local SECONDS=9
  echo "entered=$SECONDS"
  sleep 2
  echo "slept=$SECONDS"
}
SECONDS=100
count_inside
report_range outer "$SECONDS" 102 103
echo local-shadow-done

echo local-shadow-reaches-deeper
assign_deeper() {
  SECONDS=20
  echo "deeper=$SECONDS"
}
shadow_then_call() {
  local SECONDS=9
  assign_deeper
  echo "shadowing=$SECONDS"
}
SECONDS=100
shadow_then_call
sleep 1
report_range restored "$SECONDS" 101 102
echo local-shadow-reaches-deeper-done

echo subshell-base
SECONDS=50
( sleep 1; report_range child "$SECONDS" 51 52 )
report_range parent "$SECONDS" 51 52
echo subshell-base-done

echo seconds-assignment-done
