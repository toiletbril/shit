#!/bin/bash
a=(apple banana cherry)
echo "${a[@]#a}"
echo "${a[@]%y}"
echo "${a[@]^^}"
echo "${a[@],,}"
echo "${a[*]^^}"
echo "${a[*],,}"
echo "${a[1]^^}"
echo "${a[0]#ap}"
echo "${a[2]/r/R}"
echo "${a[2]//r/R}"
echo "${a[@]##*a}"
echo "${a[@]%%n*}"
b=(Foo BAR baz)
echo "${b[@],}"
echo "${b[@]^}"
echo "${b[1],,}"
echo "${b[2]^}"
