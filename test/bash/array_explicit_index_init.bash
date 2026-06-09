#!/bin/bash
b=([3]=x [7]=y)
echo "1: vals=[${b[@]}] count=${#b[@]} idx=[${!b[@]}]"
c=(a b [5]=f g)
echo "2: vals=[${c[@]}] idx=[${!c[@]}]"
d=([2]=two [0]=zero [1]=one)
echo "3: vals=[${d[@]}] idx=[${!d[@]}] d0=${d[0]}"
e=(plain [10]=ten more)
echo "4: idx=[${!e[@]}] ten=${e[10]} more=${e[11]}"
