#!/bin/bash
# Every stage of a pipeline runs in its own subshell. A variable that a stage
# word assigns through ${name:=value} or through an arithmetic expansion is
# gone once the pipeline ends. A command outside a pipeline keeps its
# assignment.

echo "== the assignment of a leading stage does not escape:"
echo "${x:=5}" | /bin/cat
echo "x=[${x-UNSET}]"

echo "== the assignment of an external leading stage does not escape:"
/bin/echo "${y:=6}" | /bin/cat
echo "y=[${y-UNSET}]"

echo "== an assignment outside a pipeline is kept:"
echo "${z:=7}"
echo "z=[${z-UNSET}]"

echo "== the assignment of a group stage does not escape:"
{ echo "${w:=8}"; } | /bin/cat
echo "w=[${w-UNSET}]"

echo "== the assignment of a trailing stage does not escape:"
/bin/cat < /dev/null | echo "${t:=10}"
echo "t=[${t-UNSET}]"

echo "== every stage of the same pipeline is confined:"
echo "${u:=11}" | /bin/cat
echo "u=[${u-UNSET}]"

echo "== a name the shell already holds keeps its value:"
a=1
echo "${a:=99}" | /bin/cat
echo "a=[${a-UNSET}]"

echo "== an arithmetic assignment in a stage word does not escape:"
echo "$(( q = 3 ))" | /bin/cat
echo "q=[${q-UNSET}]"

echo "== a stage sees the name it assigned earlier in its own words:"
echo "${m:=first}" "${m}" | /bin/cat
echo "m=[${m-UNSET}]"

echo "== a redirection target of a stage does not escape:"
directory=$(mktemp -d)
trap '[ -n "$directory" ] && /bin/rm -rf "$directory"' EXIT
echo payload > "$directory/${r:=out}" | /bin/cat
echo "r=[${r-UNSET}]"

echo "== an exported name written by a stage does not escape:"
export e
echo "${e:=exported}" | /bin/cat
echo "e=[${e-UNSET}]"
/bin/sh -c 'echo "child=[${e-UNSET}]"'
