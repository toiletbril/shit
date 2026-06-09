#!/bin/bash
# Bash [[ str =~ regex ]] match outcome, checked byte-for-byte against bash. The
# regex with grouping or alternation is held in a variable, the bash-recommended
# idiom, so the conditional lexer does not split it. Covers a match, a non-match,
# anchors, a character class, negation, and a combination under && and ||.
[[ foobar =~ oba ]] && echo m-substr
[[ foobar =~ ^foo ]] && echo m-anchor
[[ foobar =~ baz ]] || echo no-match
[[ abc123 =~ [0-9]+ ]] && echo m-class
re="^(a|b)+$"
[[ ababab =~ $re ]] && echo m-alt
[[ abcXYZ =~ $re ]] || echo no-alt
[[ ! abc =~ [0-9] ]] && echo neg-class
[[ hello =~ ell && world =~ orl ]] && echo combine-and
[[ hello =~ zzz || world =~ wor ]] && echo combine-or
