#!/bin/bash
# A {name}>file redirection on a pipeline stage allocates a descriptor the way
# it does on a plain command, checked against bash. The binding reaches a later
# redirection of the same stage and the parent keeps its own value.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

echo "== the stage binding reaches a later redirection =="
echo A {w}>"$dir/one" 3>&"$w" | /bin/cat
echo "w-after-stage=[$w]"

echo "== the stage writes through the allocated descriptor =="
/bin/sh -c 'echo through >&3' {w}>"$dir/two" 3>&"$w" | /bin/cat
echo "two=[$(cat "$dir/two")]"
echo "w-still-unset=[$w]"

echo "== an unpiped allocation binds the name in the parent =="
echo B {v}>"$dir/three"
if [ "$v" -ge 10 ]; then
  echo "v=high"
else
  echo "v=$v"
fi
exec {v}>&-

echo "== an unset name closed on a stage fails that stage =="
echo C {nope}>&- | /bin/cat
echo "closed-stage=${PIPESTATUS[0]}"

echo "== an unset name closed on a plain command fails it =="
echo D {nope}>&-
echo "closed-plain=$?"

echo done
