#!/bin/bash

# Unsetting SECONDS or RANDOM takes the dynamic reader away for the rest of the
# shell. The name reads empty until something assigns to it, and after an
# assignment it holds that value without moving. The integer attribute is gone
# too, and a subshell inherits the destroyed reader.

echo seconds-after-unset
unset SECONDS
echo "empty=[$SECONDS]"
SECONDS=3
sleep 1
echo "frozen=[$SECONDS]"

echo random-after-unset
unset RANDOM
echo "empty=[$RANDOM]"
RANDOM=3
echo "frozen=[$RANDOM][$RANDOM]"

echo attributes
declare -p SECONDS RANDOM

echo inherited
( SECONDS=9; echo "sub=[$SECONDS]" )
echo "outer=[$SECONDS]"
echo "sub-cmd=[$(echo "$RANDOM")]"

echo function-frame
report_seconds() { echo "in-fn=[$SECONDS]"; }
report_seconds

echo exported
export SECONDS=4
echo "exported=[$SECONDS]"
