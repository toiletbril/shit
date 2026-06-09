#!/bin/bash
# Nested groups and the cartesian product of several groups in one word,
# including a range crossed with a list, checked byte-for-byte against bash.
echo {{a,b},{c,d}}
echo {a,b}{c,d}{e,f}
echo {1..3}{x,y}
echo {a,b}{1..2}
echo {a..c}{1..2}
echo a{b,{c,d},e}f
echo x{1..2}{a,b}z
