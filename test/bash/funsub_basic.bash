#!/bin/bash
# The bash 5.3 funsub ${ command; } runs its body in the current shell and
# substitutes the captured stdout, so side effects persist while the output
# splices like a command substitution.
echo "simple=${ echo one; }"
echo "quoted=[${ printf 'a\nb\n\n\n'; }]"
${ persist=42; }
echo "persist=$persist"
${ pf(){ echo from_func; }; }
pf
echo "nested=${ echo $(echo inner); }"
echo "split" ${ echo a b c; }
echo "joined=[${ echo x y z; }]"
cd /tmp
echo "cwd=${ pwd; }"
IFS=-
echo "ifs=${ echo p-q-r; }"
unset IFS
