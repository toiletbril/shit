#!/bin/bash
declare -A v
k=./some/path
v[$k]=1
echo "assoc: ${v[$k]}"
a=(0 0 0)
i=2
a[$i]=9
echo "indexed: ${a[@]}"
a[$i]+=1
echo "append: ${a[2]}"
declare -A h
h[$((1+1))]=z
echo "arith: ${h[2]}"
key="x y"
declare -A m
m[$key]=found
echo "spaced: ${m[$key]}"
