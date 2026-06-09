#!/bin/bash
# Bash numeric literal bases in $(( )), checked byte-for-byte against bash.
# Covers the C hex and octal prefixes, the explicit base#digits form for several
# bases including base 64, and the digit set base 64 uses past the alphabet.
echo "hex $(( 0xff )) $(( 0XfF )) $(( 0x10 ))"
echo "oct $(( 010 )) $(( 0777 ))"
echo "based $(( 2#101 )) $(( 8#17 )) $(( 16#FF )) $(( 10#0042 ))"
echo "b36 $(( 36#z )) $(( 36#10 ))"
echo "b64 $(( 64#A )) $(( 64#a )) $(( 64#0 )) $(( 64#9 )) $(( 64#_ )) $(( 64#@ ))"
echo "mix $(( 16#A + 2#10 + 010 ))"
