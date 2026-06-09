#!/bin/bash
# Bash brace expansion of the comma-list form {a,b,c}, checked byte-for-byte
# against bash. Covers a preamble and postamble, multiple groups in a cartesian
# product, nesting, empty alternatives, a variable in an alternative, and the
# non-group cases that stay literal.
echo {a,b,c}
echo pre{a,b,c}post
echo {a,b}{1,2}
echo a{b,c}d{e,f}
echo {a,{b,c},d}
echo file{1,2,3}.txt
echo {a,b,}
echo {,a,b}
v=hi
echo {$v,x}
echo "{a,b}"
echo '{a,b}'
echo {a}
echo {}
echo path/{src,test}/main
