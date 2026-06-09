#!/bin/bash
# Bash error expansion ${v:?msg} and ${v?msg} on a present value, checked
# byte-for-byte against bash. A present value passes through without triggering
# the error path, so the output is deterministic.
val=present
echo "${val:?should not error}"
echo "${val?also fine}"
nonempty=x
echo "${nonempty:?msg}"
