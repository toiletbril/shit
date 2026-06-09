#!/bin/bash
# Brace expansion feeding command arguments and path-shaped words, checked
# byte-for-byte against bash. The expansion happens before the command sees its
# arguments, so printf and echo receive the already expanded list.
printf '%s\n' file{1,2,3}.txt
echo backup.{tar,tar.gz}
echo img{01..03}.png
echo path/{src,test}/main
echo {a..c}/{x,y}
printf '[%s]' {1..4}
echo
