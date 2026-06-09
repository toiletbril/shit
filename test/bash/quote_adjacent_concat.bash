#!/bin/bash
# Bash joins adjacent quoted and unquoted parts of one word with no separator.
# A single-quoted part, a double-quoted part, an unquoted part, and a $'...'
# part all fuse into a single argument, and expansion happens only in the parts
# that allow it.
v=mid
echo 'a'"b"c$'\x64'
echo pre"$v"post
echo "$v"'lit'$v
echo a''b""c
echo \"quoted\"and'more'
echo $'x\ty'"$v"'z'
printf '[%s]\n' "one"'two'three
