#!/bin/bash
# A redirection that cannot open its target fails the command with status 1 in
# the bash mood and the shell continues to the next command, checked against
# bash. The POSIX mood reports the same failure as status 2, covered under the
# dash comparison.
cat < /no/such/directory/file 2>/dev/null
echo "read_status=$?"
echo > /no/such/directory/file 2>/dev/null
echo "write_status=$?"
echo still_running
