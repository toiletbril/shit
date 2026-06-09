#!/bin/bash
# Bash array element assignment a[i]=v, append a+=(...), count ${#a[@]}, and
# index list ${!a[@]}, checked byte-for-byte against bash on contiguous arrays.
a=(x y z)
echo "${#a[@]}"
echo "${!a[@]}"
a[1]=Y
echo "${a[@]}"
a+=(w v)
echo "${a[@]}"
echo "${#a[@]}"
b=(1 2 3)
b[1]+=0
echo "${b[@]}"
c=(p q r)
i=2
c[i]=Z
echo "${c[@]}"
c[-1]=last
echo "${c[@]}"
arr=(a b c)
arr+=(d)
arr[0]=A
echo "${arr[@]}"
echo "count=${#arr[@]} indices=${!arr[@]}"
greet=(hello world)
echo "${#greet[1]}"
