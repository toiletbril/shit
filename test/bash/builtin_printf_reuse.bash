#!/bin/bash
# Bash printf reusing the format over extra arguments, checked byte-for-byte
# against bash. The format repeats until the arguments run out, and a format
# with several conversions consumes that many arguments per cycle.
printf '%s\n' one two three
printf '[%d]' 1 2 3 4
echo
printf '%s=%s\n' a 1 b 2 c 3
printf '%d-%d ' 1 2 3 4 5
echo
printf 'x%dy\n' 7
