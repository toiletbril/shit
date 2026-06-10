#!/bin/bash
# local NAME=$value does not word-split or glob the value, the way a plain
# assignment does not, so a value with spaces or a glob character stays one word.
f() { local x=$1; echo "[$x]"; }
f "a b c"
g() { local y=$1; echo "[$y]"; }
g "*"
