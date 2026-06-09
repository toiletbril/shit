#!/bin/bash
a=(alpha beta gamma delta epsilon)
echo "1: ${a[@]:1:2}"
echo "2: ${a[@]:2}"
echo "3: ${a[*]:0:3}"
echo "4: [${a[@]: -2}]"
echo "5: [${a[@]: -2:1}]"
echo "count: $(set -- ${a[@]:1:3}; echo $#)"
for x in "${a[@]:1:2}"; do echo "elem=$x"; done
b=(one two three)
echo "neg-len: ${b[@]:0:-1}"
