# A set -u read of an unset name aborts the subshell with status 2 the way
# dash reports it, and the parent goes on.
(set -u; echo "$not_set_here"; echo not_reached) 2>/dev/null
echo "status=$?"
echo survived
