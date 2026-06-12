#!/bin/bash
# The ${name+"${name[@]}"} idiom with a bare scalar subject keeps one field per
# element with the empty ones intact, the construct bash-completion's
# _comp_get_words passes to _comp_upvars. The bare name reads as element zero,
# so the plain form tests existence and the colon form the first element.
w=(a "")
set -- ${w+"${w[@]}"}
echo "set_with_trailing_empty=$#"
w=("" b)
set -- ${w+"${w[@]}"}
echo "set_with_leading_empty=$#"
w=()
set -- ${w+"${w[@]}"}
echo "empty_array=$#"
unset w
set -- ${w+"${w[@]}"}
echo "unset_array=$#"
w=""
set -- ${w:+"${w[@]}"}
echo "colon_empty_scalar=$#"
unset w
q=(1 2)
set -- ${w-"${q[@]}"}
echo "dash_unset_subject=$#"
w=x
set -- ${w-"${q[@]}"}
echo "dash_set_subject=$#-$1"
f() { echo "args=$#"; for a in "$@"; do printf '[%s]' "$a"; done; echo; }
words=(bash "")
f -a"${#words[@]}" words ${words[@]+"${words[@]}"}
f -a"${#words[@]}" words ${words+"${words[@]}"}
