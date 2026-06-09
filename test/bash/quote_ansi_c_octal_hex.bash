#!/bin/bash
# Bash $'...' numeric escape edges. Octal takes up to three digits so a fourth
# digit is a literal that follows, the max octal byte is \377, a hex escape
# takes up to two digits and stops at a non-hex char, and an unknown backslash
# escape keeps both the backslash and the char.
echo $'\377' | od -An -c | tr -s ' '
echo $'\1011' | od -An -c | tr -s ' '
echo $'\x41\x42' | od -An -c | tr -s ' '
echo $'\x4g' | od -An -c | tr -s ' '
echo $'\\' | od -An -c | tr -s ' '
echo $'no\qescape' | od -An -c | tr -s ' '
echo $'\060\061\062'
echo $'\x2f\x2e'
