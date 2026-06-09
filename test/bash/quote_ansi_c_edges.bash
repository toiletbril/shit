#!/bin/bash
# Bash $'...' edge escapes the existing ansi_c_quoting test does not cover.
# Four hex byte forms, the long and short unicode escapes, octal with and
# without a leading zero, \e, and a hex escape that stops at two digits before
# trailing text. Bytes go through od so a control char shows as a stable token.
echo $'\x9\x0a\x7e' | od -An -c | tr -s ' '
echo $'\u41\u42\u43'
echo $'é'
echo $'\U0001F600' | od -An -c | tr -s ' '
echo $'\0101\0102' | od -An -c | tr -s ' '
echo $'\7\10\11' | od -An -c | tr -s ' '
echo $'a\eb' | od -An -c | tr -s ' '
echo $'\x41bcd'
echo $'mix\tof\\stuff\x21'
