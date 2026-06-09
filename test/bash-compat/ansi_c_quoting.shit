#!/bin/bash
# Bash $'...' ANSI-C quoting, checked byte-for-byte against bash. Covers the
# named escapes, hex and octal bytes, a unicode escape, quote and backslash
# escapes, and concatenation with surrounding text.
echo $'hello\nworld'
echo $'tab\there'
echo $'\x41\x42\x43'
echo $'\101\102\103'
echo $'quote\'s'
echo $'back\\slash'
echo $'bell\aend' | od -An -c | tr -s ' '
echo $'é'
v=$'a\tb\tc'
echo "$v"
echo pre$'\t'post
echo $''
echo $'\e[1m' | od -An -c | tr -s ' '
printf '%s\n' $'one\ntwo'
