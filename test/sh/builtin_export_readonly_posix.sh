#!/bin/sh
# export -p and readonly list in the POSIX form dash reloads. The listing is
# filtered to the test's own names so the inherited environment cannot perturb
# the comparison.
export EV=one
echo "export_p=$(export -p | grep "^export EV=")"
readonly RV=two
echo "readonly_list=$(readonly | grep "RV=")"
