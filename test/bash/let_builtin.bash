#!/bin/bash
let "x = 5 + 3"
echo "x=$x"
let a=0; echo "zero rc=$?"
let b=7; echo "nonzero rc=$?"
i=5; let i++; let ++i
echo "steps i=$i"
let "p = 2" "q = p * 3 + 1"
echo "chain p=$p q=$q"
let "r = (1 << 4) | 2"
echo "bitwise r=$r"
