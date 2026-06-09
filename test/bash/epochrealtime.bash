#!/bin/bash
r=$EPOCHREALTIME
[[ $r == *.* ]] && echo "has-dot"
frac=${r#*.}
echo "frac-len: ${#frac}"
sec=${r%.*}
case $sec in
  [0-9]*) echo "sec-numeric" ;;
  *) echo "sec-bad" ;;
esac
echo "base: $((10#0${r%.*} > 0))"
