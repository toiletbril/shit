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
PROMPT_COMMAND='alpha; shell_session_history_check ; omega'
if [[ $PROMPT_COMMAND =~ (.*)(; *shell_session_history_check *| *shell_session_history_check *; *)(.*) ]]; then
    printf 'apple=%s|%s|%s\n' "${BASH_REMATCH[1]}" "${BASH_REMATCH[2]}" "${BASH_REMATCH[3]}"
fi
[[ 'a b' =~ (a b|c) ]] && echo 'spaced-group'
[[ ab =~ a(b) && ab == ab ]] && echo 'and-boundary'
[[ ab == ab && ( ab =~ a(b) ) ]] && echo 'paren-boundary'
