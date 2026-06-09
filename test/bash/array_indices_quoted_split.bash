#!/bin/bash
fruits=(apple banana cherry)
for i in "${!fruits[@]}"; do echo "idx $i = ${fruits[$i]}"; done
echo "joined: ${!fruits[*]}"
declare -A m=([x]=1 [y]=2)
for k in "${!m[@]}"; do echo "key $k"; done | sort
count=0
for i in "${!fruits[@]}"; do count=$((count+1)); done
echo "loopcount=$count"
