#!/bin/bash
# Bash brace sequence expansion {m..n} and {m..n..s}, numeric and alphabetic,
# checked byte-for-byte against bash. Covers ascending and descending, a step,
# zero-padding, letters, a preamble and postamble, and the cartesian product.
echo {1..5}
echo {5..1}
echo {1..10..2}
echo {0..20..5}
echo {a..e}
echo {e..a}
echo {A..F}
echo {01..05}
echo {1..3}{a..c}
echo pre{1..3}post
echo {-3..3}
echo {a..e..2}
echo file{1..3}.txt
echo {10..1..3}
echo x{1..2}y{3..4}z
