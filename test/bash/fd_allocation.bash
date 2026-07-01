#!/bin/bash
# Dynamic file descriptor allocation, the exec {var}>file form and its dup,
# read, close, and compound spellings, checked byte for byte against bash. The
# allocated number depends on the descriptors the harness leaves open, so the
# checks assert the descriptor lands at or above ten and the data flows rather
# than the absolute number, and a brace word with no adjacent redirect stays an
# argument.
tmp=/tmp/shit_bashdiff_fdalloc_$$

exec {w}>"$tmp"
echo "w>=10: $(( w >= 10 ))"
printf 'line one\n' >&$w
printf 'line two\n' >&$w
exec {w}>&-
cat "$tmp"

exec {dup}>&1
echo "dup>=10: $(( dup >= 10 ))"
printf 'through the dup\n' >&$dup
exec {dup}>&-

printf 'a\nb\n' >"$tmp"
exec {r}<"$tmp"
read first <&$r
read second <&$r
echo "read $first $second"
exec {r}<&-

exec {one}>/dev/null
exec {two}>/dev/null
echo "two distinct: $(( one != two && one >= 10 && two >= 10 ))"
exec {one}>&-
exec {two}>&-

{ echo grouped; } {g}>"$tmp"
echo "g>=10: $(( g >= 10 ))"
cat "$tmp"

echo literal {brace} stays a word

rm -f "$tmp"
