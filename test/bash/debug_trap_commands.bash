#!/bin/bash
# shellcheck disable=SC2034,SC2050,SC2086,SC2157,SC2194,SC2249

echo assignment
trap 'echo D-$BASH_COMMAND' DEBUG
plain_value=one
appended_value=first
appended_value+=second
trap - DEBUG

echo for-header
trap 'echo D-$BASH_COMMAND' DEBUG
for item in a b; do
  echo body-$item
done
trap - DEBUG

echo for-positional
set -- x y
trap 'echo D-$BASH_COMMAND' DEBUG
for item; do
  echo positional-$item
done
trap - DEBUG

echo case-header
subject=match
trap 'echo D-$BASH_COMMAND' DEBUG
case $subject in
  match) echo matched ;;
  *) echo other ;;
esac
trap - DEBUG

echo cstyle-header
trap 'echo D-$BASH_COMMAND' DEBUG
for ((index = 0; index < 2; index++)); do
  echo step-$index
done
trap - DEBUG

echo arithmetic-command
trap 'echo D-$BASH_COMMAND' DEBUG
(( 1 + 1 ))
((2 - 2))
trap - DEBUG

echo conditional-command
trap 'echo D-$BASH_COMMAND' DEBUG
[[ -n x ]]
[[ x = x && -n y ]]
trap - DEBUG

echo pipeline-commands
trap 'echo D-$BASH_COMMAND' DEBUG
printf 'payload\n' | wc -l
trap - DEBUG

echo pipeline-function
pipeline_function() {
  printf 'payload\n'
}
trap 'echo D-$BASH_COMMAND' DEBUG
pipeline_function | wc -l | tr -d ' '
trap - DEBUG

echo pipeline-function-functrace
set -T
trap 'echo D-$BASH_COMMAND' DEBUG
pipeline_function | wc -l | tr -d ' '
trap - DEBUG
set +T

echo pipeline-trap-output
trap 'echo D-$BASH_COMMAND' DEBUG
printf 'payload\n' | grep -c '^payload$'
trap - DEBUG

echo pipeline-mutation
pipeline_value=before
pipeline_debug_count=0
trap 'pipeline_value=after; pipeline_debug_count=$((pipeline_debug_count + 1)); echo D-$BASH_COMMAND' DEBUG
printf '%s\n' "$pipeline_value" | grep -c '^after$'
trap - DEBUG
printf 'value=%s count=%s\n' "$pipeline_value" "$pipeline_debug_count"

echo pipeline-lastpipe
set +m
shopt -s lastpipe
trap 'echo D-$BASH_COMMAND' DEBUG
printf 'lastpipe\n' | read pipeline_value
trap - DEBUG
printf 'value=%s\n' "$pipeline_value"
shopt -u lastpipe

echo done
