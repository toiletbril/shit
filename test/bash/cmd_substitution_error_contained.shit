#!/bin/bash
# A fatal expansion error inside a command substitution, checked against bash.
# The error exits only the substitution subshell, so the parent assignment gets
# the empty result and the script continues rather than aborting.
echo start
result=$(echo ${undef_var:?the message})
echo "after: [$result]"
echo end
