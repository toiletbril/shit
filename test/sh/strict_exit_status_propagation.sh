#!/bin/sh
# The exit status propagation across commands, pipelines, command substitution,
# and the colon null command, checked against dash. The status of the last
# command sets the special parameter, a pipeline reports its last stage, and the
# colon and an assignment-only line clear the status.

# A true and a false set the status directly.
true
echo "after_true=$?"
false
echo "after_false=$?"

# The colon null command always succeeds.
:
echo "after_colon=$?"

# A pipeline reports the status of its last stage.
false | true
echo "pipe_last_true=$?"
true | false
echo "pipe_last_false=$?"

# A command substitution does not by itself change the caller status.
false
output=$(echo captured)
echo "after_substitution=$? out=$output"

# An assignment-only command clears the status.
false
empty_assign=value
echo "after_assignment=$?"

# A subshell propagates the inner status outward.
(exit 7)
echo "subshell_status=$?"

# A grouped command reports the last command in the group.
{ false; true; }
echo "group_status=$?"
