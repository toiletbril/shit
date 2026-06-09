#!/bin/bash
# Bash [[ str =~ regex ]] matching, checked byte-for-byte against bash. A complex
# regex is held in a variable, the bash-recommended idiom, so grouping and
# alternation survive the conditional lexer.
[[ hello =~ ell ]] && echo 1
[[ hello =~ ^h.*o$ ]] && echo 2
[[ hello =~ ^x ]] || echo 3
[[ abc123 =~ [0-9]+ ]] && echo 4
[[ abc =~ [0-9]+ ]] || echo 5
v=2024-01-15
[[ $v =~ ^[0-9]+-[0-9]+-[0-9]+$ ]] && echo 6
re="(te)(st)"
[[ test =~ $re ]] && echo 7
alt="a|x"
[[ abc =~ $alt ]] && echo 8
[[ foo.bar =~ \. ]] && echo 9
word=Hello123
[[ $word =~ [A-Z][a-z]+[0-9]+ ]] && echo 10
ip=192.168.1.1
ipre="^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$"
[[ $ip =~ $ipre ]] && echo 11
