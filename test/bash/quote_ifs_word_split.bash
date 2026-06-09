#!/bin/bash
# Bash splits an unquoted expansion on IFS into separate words and drops empty
# fields from whitespace runs, while a double-quoted expansion stays one word
# with its spaces intact. A custom IFS changes the split character, and the
# quoted form ignores it.
v="one two   three"
printf '[%s]\n' $v
printf '[%s]\n' "$v"
IFS=:
p="a:b::c"
printf '[%s]\n' $p
printf '[%s]\n' "$p"
IFS=$' \t\n'
echo "---"
w="  lead trail  "
printf '[%s]\n' $w
printf '[%s]\n' "$w"
