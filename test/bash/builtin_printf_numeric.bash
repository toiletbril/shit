#!/bin/bash
# Bash printf numeric conversions with field width and precision, checked
# byte-for-byte against bash. Covers signed and unsigned bases, padding,
# alignment, and a floating conversion.
printf '%d\n' 42
printf '%5d\n' 42
printf '%-5d|\n' 42
printf '%05d\n' 42
printf '%x %X\n' 255 255
printf '%o\n' 8
printf '%.3f\n' 3.14159
printf '%8.2f\n' 3.14159
printf '%+d %+d\n' 5 -5
printf '%c\n' A
printf '%e\n' 12345.678
