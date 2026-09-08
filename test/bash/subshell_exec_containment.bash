#!/bin/bash
# A bare exec inside an in-process subshell or a command substitution must not
# move the parent's descriptors, the containment a forked subshell gets for
# free, while a top-level exec still persists.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
( exec </dev/null )
read -r line <<EOF
survives
EOF
echo "stdin=$line"
( exec >/dev/null; echo swallowed )
echo "stdout=visible"
x=$( exec >/dev/null; echo inner )
echo "subst=[$x]"
exec 9>&-
exec 3>/dev/null 4>/dev/null 5>/dev/null 6>/dev/null 7>/dev/null 8>/dev/null
( exec 9>"$dir/nine" )
{ echo leak >&9; } 2>/dev/null || echo "fd9=contained"
exec 3>&- 4>&- 5>&- 6>&- 7>&- 8>&-
( exec 2>"$dir/two"; echo contained-err >&2 )
echo "after-stderr=ok" >&2 2>/dev/null
echo "stderr-file=$(cat "$dir/two")"
exec 6>"$dir/six"
echo persisted >&6
exec 6>&-
echo "toplevel=$(cat "$dir/six")"


# A readonly or a declare -i made inside a subshell dies with the child, so the
# parent can still reassign the name afterward, the way a forked subshell
# isolates its option and attribute changes.
x=1
(readonly x)
x=2
echo "x=$x"
n=5
(declare -i n)
n=abc
echo "n=$n"
arr=(a b)
(readonly arr)
arr=(c d)
echo "arr=${arr[1]}"

# A fatal expansion error inside a command substitution, checked against bash.
# The error exits only the substitution subshell, so the parent assignment gets
# the empty result and the script continues rather than aborting.
echo start
result=$(echo ${undef_var:?the message})
echo "after: [$result]"
echo end

# A not-found command in a pipeline stage does not abort the rest, so a later
# stage still runs and the pipeline status is the last stage's, matching bash.
nonexistent_cmd_xyz_123 | cat
echo "after=$?"
echo start | nonexistent_cmd_xyz_123 | wc -l | tr -d ' '

# A subshell that moves a descriptor has to keep the original somewhere until it
# ends. The place is outside the range a script writes by hand, and the low
# numbers read as closed the way they do in a forked subshell. Both shells park
# the backup of the loop's own stderr redirection on 10. The printed list stays
# non-empty in a healthy run.
echo "== subshell exec backup =="
exec 3>"$dir/three"
( exec 3>&-
  visible=""
  number=10
  while [ "$number" -le 13 ]; do
    if : >&"$number"; then
      visible="$visible $number"
    fi
    number=$((number + 1))
  done 2>/dev/null
  printf 'backups in reach:%s\n' "$visible" )
echo restored >&3
exec 3>&-
echo "three=$(cat "$dir/three")"

# Two descriptors moved at two depths take two backups. Each one returns to its
# own number when the subshell that moved it ends.
echo "== nested subshell backups =="
exec 4>"$dir/four" 5>"$dir/five"
( exec 4>&-
  ( exec 5>&-
    if echo deepest >&5; then echo "deep=open"; else echo "deep=contained"; fi )
  if echo inner >&4; then echo "inner=open"; else echo "inner=contained"; fi
  echo inner-five >&5 ) 2>/dev/null
echo outer-four >&4
echo outer-five >&5
exec 4>&- 5>&-
echo "four=$(cat "$dir/four")"
echo "five=[$(cat "$dir/five")]"

# The backup is placed under the open file limit. A tight limit lowers the
# ceiling toward the range a script writes by hand, and the descriptor still has
# to come back. This section runs last because the limit outlives it.
echo "== tight open file limit =="
ulimit -n 24
exec 7>"$dir/seven"
( exec 7>&- )
echo returned >&7
exec 7>&-
echo "seven=$(cat "$dir/seven")"
