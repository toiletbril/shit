#!/bin/bash
# export and readonly list their variables in the declare form bash reloads, and
# export -n removes the export attribute while keeping the variable in the shell.
# The listing is filtered to the test's own names so the inherited environment
# cannot perturb the comparison.
export EV=one
echo "export_p=$(export -p | grep '^declare -x EV=')"
echo "export_bare=$(export | grep '^declare -x EV=')"
export -n EV=ignored
echo "unmark_env=$(env | grep -c '^EV=')"
echo "unmark_shell=$EV"
readonly RV=two
echo "readonly_p=$(readonly -p | grep '^declare -r RV=')"
