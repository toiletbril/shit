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
( exec 5>"$dir/five" )
{ echo leak >&5; } 2>/dev/null || echo "fd5=contained"
( exec 2>"$dir/two"; echo contained-err >&2 )
echo "after-stderr=ok" >&2 2>/dev/null
echo "stderr-file=$(cat "$dir/two")"
exec 6>"$dir/six"
echo persisted >&6
exec 6>&-
echo "toplevel=$(cat "$dir/six")"
