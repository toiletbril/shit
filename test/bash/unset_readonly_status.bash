#!/bin/bash
# unset of a read-only variable fails with status one, the same as bash, and
# the shell keeps running.
readonly x=5
unset x 2>/dev/null
echo "status=$?"
echo after
