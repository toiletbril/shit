#!/bin/env bash
arr=("a b" c)
printf '[%s]' ${arr[@]+"${arr[@]}"}; echo
empty=()
printf '<%s>' ${empty[@]+"${empty[@]}"}; echo "(done)"
for x in ${arr[@]+"${arr[@]}"}; do echo "iter=$x"; done
