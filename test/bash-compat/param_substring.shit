#!/bin/bash
# Bash substring parameter expansion ${v:offset:length}, checked byte-for-byte
# against bash. Covers positive and negative offsets, negative lengths, an
# arithmetic offset from a variable, and out-of-range clamping.

v=abcdefgh
echo "${v:2}"
echo "${v:2:3}"
echo "${v:0:4}"
echo "${v: -3}"
echo "${v: -3:2}"
echo "${v:2:-1}"
echo "${v:20}"
echo "[${v:8}]"

short=abc
echo "${short:1:10}"

n=2
len=3
echo "${v:n:len}"
echo "${v:n+1:2}"

# The colon modifiers still parse as themselves, not as a substring.
unset u
echo "${u:-default}"
echo "${u:+alt}"
val=set
echo "${val:+yes}"

# A negative offset that reaches before the start yields empty, not the whole
# value.
echo "[${v: -100}]"
echo "[${short: -50}]"
