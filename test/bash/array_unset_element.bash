#!/bin/bash
a=(a b c d e)
unset 'a[2]'
echo "vals=${a[@]} count=${#a[@]}"
declare -A cap=([france]=paris [japan]=tokyo [italy]=rome)
unset 'cap[france]'
echo "capcount=${#cap[@]} japan=${cap[japan]} france=[${cap[france]}]"
b=(x y z)
unset 'b[-1]'
echo "b=${b[@]}"
