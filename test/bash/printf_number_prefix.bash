#!/bin/bash
# printf parses the leading numeric prefix of a malformed integer argument the
# way bash does, checked byte-for-byte on standard output against bash. The
# diagnostic and the exit status are not compared here since the harness reads
# standard output alone.
printf '%d\n' 12abc
printf '%d\n' 9z
printf '%d\n' '  42  '
printf '%d\n' 010
printf '%d\n' 0x1f
printf '%d\n' ' 7'
printf '[%d]\n' ''
printf '%d\n' abc
