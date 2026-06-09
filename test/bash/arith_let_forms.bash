#!/bin/bash
# Bash let builtin arithmetic forms, checked byte-for-byte against bash. Covers a
# parenthesized expression, the assignment operators through let, increment and
# decrement, a multi-argument let where a later argument reads an earlier
# assignment, and the let exit status that follows the last argument value.
let "v = (3 + 4) * 2 - 1"
echo "v $v"
let w=10 "w += 5" "w *= 2"
echo "w $w"
let p=3 q=4 "r = p * q"
echo "r $r"
i=5
let i++
let ++i
let i--
echo "i $i"
let "z = 7" "z <<= 2"
echo "z $z"
let "0"; echo "rc-zero $?"
let "9"; echo "rc-nonzero $?"
