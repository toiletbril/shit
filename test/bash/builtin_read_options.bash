#!/bin/bash
# read -n reads at most the given number of bytes, and read -u reads from the
# named descriptor rather than the standard input.
printf 'abcdef' | { read -n 3 x; echo "n=[$x]"; }
printf 'hello\n' | { read -n 0 z; echo "n0=rc$? [$z]"; }
file=$(mktemp)
printf 'fromfd\n' > "$file"
exec 7< "$file"
read -u 7 y
echo "u=[$y]"
exec 7<&-
rm -f "$file"
