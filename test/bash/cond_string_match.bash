#!/bin/bash
# Bash [[ ]] string comparison and pattern matching, checked byte-for-byte
# against bash. Covers = == != with ASCII operands, the < and > collation order,
# an unquoted right side that matches as a glob, and a quoted right side whose
# operand equals it exactly.
[[ apple = apple ]] && echo single-eq
[[ apple == apple ]] && echo double-eq
[[ apple != orange ]] && echo neq
[[ aaa < bbb ]] && echo lt-str
[[ zzz > aaa ]] && echo gt-str
[[ abc == a*c ]] && echo glob-star
[[ abc == a?c ]] && echo glob-question
y="a*c"
[[ $y == "a*c" ]] && echo quoted-literal-match
[[ "a*c" == "a*c" ]] && echo quoted-both
v=hello.txt
[[ $v == *.txt ]] && echo suffix-match
