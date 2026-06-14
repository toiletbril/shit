#!/bin/bash
# A [[ =~ ]] right side that joins a variable expansion with a regex group, an
# alternation, or a bracket must expand the variable and keep the metacharacters
# live, the construct sdkman's sdk use performs.
d=/usr/local
p=/usr/local/bin/foo
if [[ $p =~ ${d}/([^/]+) ]]; then echo "g1=${BASH_REMATCH[1]}"; fi
[[ cats =~ ^(cat|dog)s$ ]] && echo "alt=${BASH_REMATCH[1]}"
v=x
[[ xy =~ ${v}(y) ]] && echo "grp=${BASH_REMATCH[1]}"
[[ axb =~ a"."b ]] && echo "quoted-dot-literal-matched" || echo "quoted-dot-literal-no-match"
[[ a.b =~ a"."b ]] && echo "quoted-dot-exact-matched"
