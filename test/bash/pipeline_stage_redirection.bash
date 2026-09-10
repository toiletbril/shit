#!/bin/bash
# A pipeline stage owns its own redirections, its own status, and its own end
# of every pipe. The claims measured here are the pipefail selection, the
# destination an unresolved stage sends its diagnostic to, the survival of the
# remaining stages after one stage cannot open its target, the target a
# repeated output redirection binds to, and the signal a producer receives once
# its consumer has left.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

echo "== pipefail selection =="
set -o pipefail
( exit 3 ) | ( exit 5 ) | ( exit 0 )
echo "rightmost=$?"
( exit 3 ) | ( exit 0 ) | ( exit 0 )
echo "leftmost-only=$?"
( exit 0 ) | ( exit 0 ) | ( exit 0 )
echo "all-zero=$?"
set +o pipefail

echo "== unresolved stage redirection =="
kosh_absent_stage_command 2> "$dir/notfound" | cat
echo "status=${PIPESTATUS[0]}"
if [ -s "$dir/notfound" ]; then
  echo "diagnostic=redirected"
fi

echo "== failing stage redirection =="
{ echo alpha > "$dir/missing/f"; } | { cat; echo "later-stage-ran"; }

echo "== repeated output target =="
{ echo one > "$dir/first" > "$dir/second"; } | cat
echo "first=[$(< "$dir/first")]"
echo "second=[$(< "$dir/second")]"

echo "== producer after the consumer leaves =="
yes | { head -1 > /dev/null; }
echo "group-consumer=${PIPESTATUS[0]}"
yes | ( head -1 > /dev/null )
echo "subshell-consumer=${PIPESTATUS[0]}"
{ yes; } | head -1 > /dev/null
echo "group-producer=${PIPESTATUS[0]}"

echo "== compound stage end of file =="
( echo subshell-stage ) | ( cat )
{ echo group-stage; } | { cat; }
