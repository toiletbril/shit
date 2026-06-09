#!/bin/bash
a=(hello world)
echo "elem: ${a[0]//l/L}"
b=(aXa bXb cXc)
printf '[%s]' "${b[@]//X/-}"; echo
echo "star: ${b[*]//X/+}"
d=(local -A NAME)
g() { "${d[@]//NAME/myassoc}"; myassoc[key/with/slash]=v; echo "got: ${myassoc[key/with/slash]}"; }
g
