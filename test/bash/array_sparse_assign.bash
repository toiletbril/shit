#!/bin/bash
a=(x y z)
a[5]=q
echo "at5: vals=[${a[@]}] count=${#a[@]} idx=[${!a[@]}]"
b=([3]=x [7]=y)
echo "init: vals=[${b[@]}] count=${#b[@]} idx=[${!b[@]}]"
c=()
c[10]=hello
echo "single: vals=[${c[@]}] count=${#c[@]} idx=[${!c[@]}]"
