#!/bin/bash
# A prefix assignment on a pipeline stage reaches that stage's environment, the
# plain, the append, and the integer-append forms, and does not persist after.
x=zz env | grep -c "^x=zz"
echo "x_after=[$x]"
export y=ab
y+=cd env | grep "^y="
echo "y_after=$y"
declare -ix w=5
w+=3 env | grep "^w="
echo "w_after=$w"
