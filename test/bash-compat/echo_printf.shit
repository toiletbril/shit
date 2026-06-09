#!/bin/bash
# Bash echo -e/-E/-n and printf -v, checked byte-for-byte against bash. The bash
# echo leaves escapes literal unless -e, and printf -v stores into a variable.
echo -e "a\tb\tc"
echo -e "line1\nline2"
echo "no\tescapes here"
echo -ne "x\ty\n"
echo -E "stays\tliteral"
echo plain words
echo -e "bell\aafter" | od -An -c | tr -s ' '
printf -v x "%d-%d" 3 5
echo "x is $x"
printf -v greeting "Hello, %s!" world
echo "$greeting"
printf -v padded "%05d" 42
echo "$padded"
printf "%s %s\n" direct output
