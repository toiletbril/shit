#!/bin/sh
# Command substitution, positional parameters, redirection, and pipelines.

set -- one two three four
echo "count=$#"
echo "first=$1 third=$3"
shift
echo "after shift: $1 $#"
shift 2
echo "after shift 2: $1 $#"

result=$(echo "  spaced  " | cat)
echo "[$result]"

tmp=/tmp/shit_everything_$$
printf '%s\n' gamma alpha beta > "$tmp"
echo "sorted:"
sort "$tmp"
echo "lines: $(wc -l <"$tmp")"
rm -f "$tmp"

count=0
for item in $(echo a b c d); do
    count=$((count + 1))
done
echo "iterated $count items"

echo "pipeline: $(printf '5\n3\n9\n1\n' | sort -n | head -n1)"

i=1
until [ "$i" -gt 3 ]; do
    echo "until $i"
    i=$((i + 1))
done
