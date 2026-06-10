#!/bin/bash
# declare -i marks a name so every assignment evaluates as arithmetic, += adds
# rather than concatenates, +i and unset clear the mark, and -p prints it.
declare -i x=5
x+=3
echo "add=$x"
declare -i y="5+5"
echo "expr=$y"
v=5
declare -i v+=3
echo "same_command=$v"
declare -i u=4
declare +i u
u+=3
echo "unmarked=$u"
declare -i t=7
unset t
t=1+1
echo "unset_clears=$t"
declare -i s=2
declare -p s
declare -ix r=3
declare -p r
env | grep '^r='
declare -i q=5
q+="2,3"
echo "comma=$q"
declare -i p=5
p+=
echo "empty=[$p]"
declare -i o
o=abc
echo "invalid=[$o]"
declare -i n=5
export n+=3
echo "export=$n"
env | grep '^n='
typeset -i m=3
m+=4
echo "typeset=$m"
