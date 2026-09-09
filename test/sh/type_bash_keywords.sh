#!/bin/sh
for name in function select "[[" "]]"; do
  type "$name" 2> /dev/null
  echo "type_$name=$?"
  command -v "$name" 2> /dev/null
  echo "command_v_$name=$?"
done
type if
echo "type_if=$?"
type while
echo "type_while=$?"
command -v case
echo "command_v_case=$?"
echo done
