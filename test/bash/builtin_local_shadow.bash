#!/bin/bash
# Bash local inside a function shadowing an outer variable then restoring it on
# return, checked byte-for-byte against bash. A callee that does not redeclare
# the name sees the dynamic-scope value of the caller.
v=outer
show() { echo "in show: $v"; }
mid() { local v=inner; echo "in mid: $v"; show; }
echo "before: $v"
mid
echo "after: $v"
counter() { local v=$1; echo "counter sees $v"; }
counter 99
echo "still: $v"
