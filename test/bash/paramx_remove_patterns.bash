#!/bin/bash
# Bash prefix and suffix pattern removal ${v#p} ${v##p} ${v%p} ${v%%p}, checked
# byte-for-byte against bash. Covers shortest and longest match, glob bracket
# classes, an empty value, and a no-match left unchanged.
path=/usr/local/bin/prog
echo "${path#*/}"
echo "${path##*/}"
echo "${path%/*}"
echo "${path%%/*}"
file=archive.tar.gz
echo "${file%.*}"
echo "${file%%.*}"
echo "${file#*.}"
echo "${file##*.}"
v=hello
echo "${v#x}"
echo "${v%x}"
echo "${v#[hH]}"
echo "${v%[oO]}"
empty=
echo "[${empty#x}]"
url=http://example.com/path
echo "${url#http://}"
echo "${url%/*}"
