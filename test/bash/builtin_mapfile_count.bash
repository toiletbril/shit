#!/bin/bash
printf 'w\nx\ny\nz\n' | { mapfile -t -n 2 v; echo "n=${#v[@]} first=${v[0]} last=${v[1]}"; }
printf 'a\nb\nc\n' | { mapfile -t -n 0 all; echo "all=${#all[@]}"; }
printf '1\n2\n3\n4\n5\n' | { mapfile -t -n3 three; echo "three=${#three[@]} ${three[2]}"; }
printf 'p\nq\n' | { mapfile -t both; echo "both=${#both[@]}"; }
