#!/bin/bash
# The format string and a %b argument both decode the \NNN octal and \xHH hex
# escapes, and a \x without a hex digit stays literal on stdout.
printf 'a\0b\n' | od -An -tx1 | tr -s ' '
printf '\101\102\060\n' | od -An -tx1 | tr -s ' '
printf '\x41\x42\n' | od -An -tx1 | tr -s ' '
printf '%b' '\x41\n' | od -An -tx1 | tr -s ' '
printf '%b' '\101\n' | od -An -tx1 | tr -s ' '
printf '%b' '\0101\n' | od -An -tx1 | tr -s ' '
printf '%b' '\x4\n' | od -An -tx1 | tr -s ' '
printf '%b' '\x\n' 2>/dev/null | od -An -tx1 | tr -s ' '
printf '%b' 'a\0b\n' | od -An -tx1 | tr -s ' '
