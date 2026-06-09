#!/bin/sh
# The printf builtin reuses the format across extra arguments, pads and aligns
# fields, converts integers and a leading character, and expands the percent-b
# escapes, checked against dash.

# The format string repeats until the arguments run out, padding a missing one.
printf '%s|%s\n' a b c d e

# Integer conversion with zero padding and a width.
printf '%05d\n' 42
printf '%d and %d\n' 7 -3

# Hex and octal conversion of an integer.
printf 'hex=%x oct=%o\n' 255 8

# Left alignment and right alignment of a string field.
printf '[%-6s][%6s]\n' hi hi

# The percent-c conversion takes the first character of its argument.
printf '%c%c%c\n' apple banana cherry

# The percent-b conversion expands backslash escapes in the argument.
printf '%b\n' 'one\ttwo'

# A literal percent prints a single percent.
printf '100%%\n'

# A numeric string converts even with surrounding format text.
printf 'value: %d\n' 256
