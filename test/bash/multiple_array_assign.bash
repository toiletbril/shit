#!/bin/bash
a=() b=(); echo "empties: ${#a[@]} ${#b[@]}"
flags= pvars=() specs=(); echo "mixed: [$flags] ${#pvars[@]} ${#specs[@]}"
x=1 y=(p q r); echo "scalar-then-array: $x ${y[2]}"
p=(1 2) q=() r=(9); echo "three: ${p[1]} ${#q[@]} ${r[0]}"
