#!/bin/bash
# The alternate or default word of an array-subject modifier follows its own
# spelling, so an inner "${b[*]}" joins on the first IFS byte into one field
# while an inner "${b[@]}" keeps one field per element, whatever the subject
# uses. The subject's own [*] still joins when the - form emits the elements.
a=(x y)
b=(p q r)
IFS=-
printf '[%s]\n' ${a[@]+"${b[*]}"}
printf '[%s]\n' "${a[@]+${b[*]}}"
printf '[%s]\n' ${a[@]+"${b[@]}"}
unset c
printf '[%s]\n' ${c[@]-"${b[*]}"}
printf '[%s]\n' "${a[*]-unused}"
set -- ${a[@]+"${b[*]}"}
echo "n=$#"
unset IFS
printf '[%s]\n' ${a[@]+"${b[*]}"}
