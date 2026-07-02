#!/bin/bash
# A variable read inside arithmetic is re-evaluated as an arithmetic
# expression, the same as bash. A value with surrounding blanks, an operator,
# an indirect name, a sign, or a hex form all resolve, and an array subscript
# reads the same way.
x=" 5 "; echo $((x + 1))
y="2*3"; echo $((y))
a=c; c=7; echo $((a))
n=-4; echo $((n * 2))
h=0x10; echo $((h + 1))
w="  7  "; echo $((w))
z=" 3 "; arr=(0 10 20 30); echo "${arr[z]}"
