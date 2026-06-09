#!/bin/bash
# Bash single quotes keep every byte literal, no backslash, dollar, or backtick
# is special, and a single quote cannot appear inside. A lone backslash and an
# embedded newline are kept as written.
echo 'no $expand `cmd` \t \n \\ "dq" literal'
v=World
echo 'single $v stays'
echo '\'
printf '%s\n' 'a
b'
echo 'tab	and spaces   kept' | od -An -c | tr -s ' '
