#!/bin/bash
# A bare tilde before a colon expands the same as bash. In an assignment value
# each colon-delimited segment expands its leading tilde, and a regular word
# expands only its own leading tilde while a tilde after a colon stays literal.
# A quoted tilde is left alone.
p=~:~; echo "$p"
x=~/a:~/b; echo "$x"
echo ~:x
echo ~/a:~/b
echo ~:~
echo a:~
echo "~:x"
