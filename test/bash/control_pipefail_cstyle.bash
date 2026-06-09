#!/bin/bash
# set -o pipefail effect on pipeline $?, and the C-style for loop.

set -o pipefail
false | true
echo "pipefail false|true rc $?"
true | false
echo "pipefail true|false rc $?"
true | true
echo "pipefail true|true rc $?"
set +o pipefail
false | true
echo "no pipefail false|true rc $?"

for (( i = 0; i < 5; i++ )); do
    echo "cfor $i"
done

sum=0
for (( k = 1; k <= 4; k++ )); do
    sum=$((sum + k))
done
echo "cfor sum $sum"

# nested C-style for with break N.
for (( a = 0; a < 3; a++ )); do
    for (( b = 0; b < 3; b++ )); do
        if (( a == 1 && b == 1 )); then
            break 2
        fi
        echo "cell $a $b"
    done
done
echo "done"
