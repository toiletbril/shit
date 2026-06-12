#!/bin/bash
# FUNCNAME reads inside a function in its scalar and array forms, and OSTYPE
# reads the platform, the dynamic variables a sourced config relies on.
outer() {
  echo "scalar=${FUNCNAME}"
  echo "zero=${FUNCNAME[0]}"
  inner
}
inner() {
  echo "depth=${#FUNCNAME[@]}"
  echo "stack=${FUNCNAME[*]}"
}
outer
[[ $OSTYPE == linux* || $OSTYPE == darwin* || $OSTYPE == msys* ]] &&
  echo "ostype=known"
f() { unset -f "$FUNCNAME"; }
f
command -v f >/dev/null || echo "unset_self=gone"
