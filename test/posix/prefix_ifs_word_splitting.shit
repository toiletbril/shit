#!/bin/sh
# A prefix IFS=... assignment drives the word splitting of the command it
# precedes, including the read builtin, and does not persist after it, checked
# against dash. This is the splitting config.sub relies on.

IFS=- read a b c d <<HD
w-x-y-z
HD
echo "[$a][$b][$c][$d]"

# A second prefixed read splits on a different separator, and the last name
# receives the remainder of the line.
IFS=: read p q <<HD
left:right:extra
HD
echo "[$p][$q]"

# IFS is back to its whitespace default after the prefixed commands, so a later
# read splits on spaces rather than the prior separators.
printf 'one two three\n' | { read first rest; echo "[$first][$rest]"; }
