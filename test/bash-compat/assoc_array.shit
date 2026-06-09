#!/bin/bash
# Bash associative arrays via declare -A, checked byte-for-byte against bash with
# literal keys (the multi-key element order is store-defined, so single-element
# views are used for the listing forms).
declare -A m
m[foo]=bar
echo "${m[foo]}"
m[key]=value
echo "${m[key]}"
echo "${#m[@]}"
declare -A colors
colors[red]=ff0000
echo "${colors[red]}"
echo "${!colors[@]}"
echo "${colors[@]}"
declare -A counts
counts[apples]=5
counts[apples]=8
echo "${counts[apples]}"
declare -A app
app[x]=foo
app[x]+=bar
echo "${app[x]}"
typeset -A t
t[k]=v
echo "${t[k]}"
declare -A empty
echo "[${empty[missing]}] count=${#empty[@]}"
