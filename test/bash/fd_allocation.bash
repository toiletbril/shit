#!/bin/bash
# Dynamic file descriptor allocation, the exec {var}>file form and its dup,
# read, close, and compound spellings, checked byte for byte against bash. The
# allocated numbers are printed, so the lowest free descriptor at or above ten
# is observed, and a brace word with no adjacent redirect stays an argument.
tmp=/tmp/shit_bashdiff_fdalloc_$$

exec {w}>"$tmp"
echo "w=$w"
printf 'line one\n' >&$w
printf 'line two\n' >&$w
exec {w}>&-
cat "$tmp"

exec {dup}>&1
echo "dup=$dup"
printf 'through the dup\n' >&$dup
exec {dup}>&-

printf 'a\nb\n' >"$tmp"
exec {r}<"$tmp"
read first <&$r
read second <&$r
echo "read $first $second from $r"
exec {r}<&-

exec {one}>/dev/null
exec {two}>/dev/null
echo "two allocations $one $two"
exec {one}>&-
exec {two}>&-

{ echo grouped; } {g}>"$tmp"
echo "g=$g"
cat "$tmp"

echo literal {brace} stays a word

rm -f "$tmp"
