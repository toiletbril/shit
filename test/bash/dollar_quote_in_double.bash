#!/bin/bash
# Inside double quotes $'...' is not ANSI-C quoting, so the $' is literal and
# bash prints the three bytes. It decodes only in an unquoted or bare context.
echo "$'x'"
echo "a$'\t'b"
echo "$'\n'"
printf '%s\n' $'a\tb'
