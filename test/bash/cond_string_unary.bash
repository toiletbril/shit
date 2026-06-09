#!/bin/bash
# Bash [[ ]] string unary tests, checked byte-for-byte against bash. Covers -z on
# an empty and a non-empty operand, -n likewise, the -v test on a set and an
# unset variable, and the negation of each.
[[ -z "" ]] && echo z-empty
[[ -z "x" ]] || echo z-nonempty-false
[[ -n "x" ]] && echo n-nonempty
[[ -n "" ]] || echo n-empty-false
[[ ! -z "x" ]] && echo not-z
[[ ! -n "" ]] && echo not-n
unset uv
[[ -v uv ]] || echo v-unset-false
sv=1
[[ -v sv ]] && echo v-set
empty=
[[ -v empty ]] && echo v-set-empty
[[ ! -v uv ]] && echo not-v-unset
