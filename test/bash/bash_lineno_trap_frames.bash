#!/bin/bash

# BASH_LINENO holds the call-site line of each frame BASH_SOURCE names, so a
# script with no function call still holds one zero.
echo "top-count=${#BASH_LINENO[@]}"
echo "top-all=[${BASH_LINENO[*]}]"
echo "top-zero=[${BASH_LINENO[0]}]"
echo "top-funcname-count=${#FUNCNAME[@]}"
echo "top-scalar=[$BASH_LINENO]"

echo nested
report_frames() {
  echo "frames ln=[${BASH_LINENO[*]}] fn=[${FUNCNAME[*]}]"
  echo "frames-scalar=[$BASH_LINENO]"
}
level_two() {
  report_frames
}
level_one() {
  level_two
}
level_one

echo err-trap
set -E -T
trap 'echo "err ln=[${BASH_LINENO[*]}] fn=[${FUNCNAME[*]}]"' ERR
inner_fail() {
  false
}
outer_fail() {
  inner_fail
}
outer_fail
echo "err-status=$?"
trap - ERR

echo return-trap
trap 'echo "ret ln=[${BASH_LINENO[*]}] fn=[${FUNCNAME[*]}]"' RETURN
returning_inner() {
  :
}
returning_outer() {
  returning_inner
}
returning_outer
trap - RETURN
set +E +T

echo trailing-fire
set -E
trap 'echo "late ln=[${BASH_LINENO[*]}] fn=[${FUNCNAME[*]}]"' ERR
false
echo "late-status=$?"
trap - ERR
set +E

echo done
