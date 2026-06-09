#!/bin/bash
# Bash printf -v assigning the formatted result into a variable, checked
# byte-for-byte against bash. Covers numeric, padded, multi-conversion, reused,
# floating, and star-width forms.
printf -v num "%d" 100
echo "num=$num"
printf -v hex "%04x" 255
echo "hex=$hex"
printf -v multi "%s/%s" a b
echo "multi=$multi"
printf -v reused "[%d]" 1 2 3
echo "reused=$reused"
printf -v floatv "%.2f" 3.14159
echo "floatv=$floatv"
printf -v widthv "%*d" 6 7
echo "widthv=[$widthv]"
