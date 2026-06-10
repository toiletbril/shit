#!/bin/bash
# The += append form on declare, local, and export concatenates onto the current
# value rather than dropping the operator, while a += on a plain command word
# stays literal.
declare d=foo
declare d+=bar
echo "$d"
export E=ab
export E+=cd
echo "$E"
f() { local p=xy; local p+=z; echo "$p"; }
f
echo k+=v
