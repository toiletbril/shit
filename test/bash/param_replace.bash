#!/bin/bash
# Bash pattern-replacement expansion ${v/pat/rep} and its variants, checked
# byte-for-byte against bash. Covers first and all, start and end anchors, glob
# patterns, deletion, a pattern from a variable, and an escaped slash.

v=hello
echo "${v/l/L}"
echo "${v//l/L}"
echo "${v/#he/HE}"
echo "${v/%lo/LO}"
echo "${v/l}"
echo "${v/x/y}"

csv=a,b,c,d
echo "${csv//,/ }"
echo "${csv/,/;}"

cls=hello
echo "${cls//[lo]/_}"

rep=aaa
echo "${rep//a/bb}"
echo "${rep//a/}"

p=l
echo "${v//$p/L}"

path=a/b/c
echo "${path//\//_}"

greedy=axbxc
echo "${greedy/x*x/Y}"

word=Hello
echo "${word/#H/J}"
echo "${word/%o/0}"
echo "${word/#x/J}"
echo "${word/%x/0}"

amp=hello
echo "${amp/l/[&]}"
echo "${amp//l/<&>}"
echo "${amp/e/\&}"
dotted=a.b.c
echo "${dotted//./[&]}"
