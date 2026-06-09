#!/bin/bash
i=1 j=2
a[i|j]=ored
echo "or: ${a[3]}"
b[2&3]=anded
echo "and: ${b[2]}"
c[1<<4]=shifted
echo "shift: ${c[16]}"
key[63|8]=1
echo "mask: ${key[55]}-${key[8]}"
declare -A m
m[plain]=v
echo "assoc: ${m[plain]}"
