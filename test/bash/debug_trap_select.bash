#!/bin/bash
# shellcheck disable=SC2034,SC2086,SC2249

echo select-header
trap 'echo D-$BASH_COMMAND' DEBUG
select item in a b; do
  echo picked-$item
  break
done <<< '2'
trap - DEBUG

echo select-empty
set --
trap 'echo D-$BASH_COMMAND' DEBUG
select item; do
  break
done < /dev/null
trap - DEBUG

echo select-positional
set -- x y
trap 'echo D-$BASH_COMMAND' DEBUG
select item; do
  echo chose-$item
  break
done <<< '1'
trap - DEBUG

echo select-expanded
words='a b'
trap 'echo D-$BASH_COMMAND' DEBUG
select item in $words "two words"; do
  echo took-$item
  break
done <<< '3'
trap - DEBUG

echo done
