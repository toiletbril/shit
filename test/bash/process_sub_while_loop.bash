#!/bin/bash
# A process substitution as a while loop's redirection target is read correctly
# across every iteration and the loop runs to completion, the common idiom
# done < <(cmd), and a later command still runs after the loop.
while read -r line; do
  echo "got:$line"
done < <(printf 'a\nb\nc\n')
echo after
total=0
while read -r n; do
  total=$((total + n))
done < <(printf '10\n20\n30\n')
echo "total=$total"
