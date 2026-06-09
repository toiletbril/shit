#!/bin/bash
# Sequence ranges with a step and zero-padding, including descending order, a
# negative step, a wider pad than the value, and degenerate single-value ranges,
# checked byte-for-byte against bash.
echo {001..10}
echo {a..z..3}
echo {z..a..5}
echo {10..1..2}
echo {-5..5..2}
echo {00..05}
echo {1..10..-2}
echo {10..1..-2}
echo {1..1}
echo {a..a}
echo {0..-5}
