#!/bin/bash
# A local += on a name that shadows an outer one starts from empty the way bash
# localizes it fresh, while a re-declared local in the same scope appends to its
# own value, and the outer name is restored when the function returns.
x=glob
f() { local x+=ADD; echo "[$x]"; }
f
echo "[$x]"
g() { local y=a; local y+=b; echo "[$y]"; }
g
