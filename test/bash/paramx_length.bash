#!/bin/bash
# Bash length expansion ${#v}, checked byte-for-byte against bash. Covers a set
# value, an empty value, an unset value, an array element count, and individual
# element lengths.
v=hello
echo "${#v}"
empty=
echo "${#empty}"
unset u
echo "${#u}"
long=abcdefghij
echo "${#long}"
arr=(one two three)
echo "${#arr[@]}"
echo "${#arr[0]}"
echo "${#arr[2]}"
spaces="a b c"
echo "${#spaces}"
