#!/bin/bash
# Bash line continuation. A backslash at end of line is removed with its
# newline outside quotes and inside double quotes, so the two halves join. A
# backslash newline inside single quotes is kept literal, and inside $'...' a
# backslash newline is removed.
echo one\
two
echo "three\
four"
echo 'five\
six' | od -An -c | tr -s ' '
echo $'seven\
eight'
abc\
def=ok
echo "$abcdef"
echo before \
after
