#!/bin/bash
# Bash case-modification expansion ${v^} ${v^^} ${v,} ${v,,} with optional glob,
# checked byte-for-byte against bash.
v=hello
echo "${v^}"
echo "${v^^}"
u=HELLO
echo "${u,}"
echo "${u,,}"
echo "${v^^l}"
echo "${v^^[aeiou]}"
m=abcABC
echo "${m,,}"
echo "${m^^}"
n=123abc
echo "${n^^}"
mixed=mIxEd
echo "${mixed^}"
echo "${mixed,}"
phrase="the quick fox"
echo "${phrase^^}"
echo "${phrase^^[tqf]}"
