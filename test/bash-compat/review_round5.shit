#!/bin/bash
# Round-five review edge cases, checked byte-for-byte against bash. The bash echo
# reads octal only in the \0NNN form so a bare \NNN stays literal, and assigning
# an element to a scalar-valued name promotes the scalar to element zero.
echo -e '\101 \0101'
echo -e '\0102 end'
echo -e 'lit\1eral'
a=5
a[2]=99
echo "${a[0]} ${a[1]} ${a[2]}"
b=hello
b[0]=10
echo "${b[0]}"
c=world
c[1]=second
echo "${c[0]}-${c[1]}-count=${#c[@]}"
