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

echo pipeline-compound-stages
trap 'echo D-$BASH_COMMAND' DEBUG
{ echo group-a; echo group-b; } | wc -l | tr -d ' '
( echo subshell-stage ) | cat
if true; then echo if-stage; fi | wc -l | tr -d ' '
while false; do :; done | cat
for word in a b; do echo for-$word; done | wc -l | tr -d ' '
echo nested | cat | { cat; } | wc -l | tr -d ' '
trap - DEBUG

echo pipeline-stderr-merge
trap 'echo D-$BASH_COMMAND' DEBUG
printf 'merged\n' |& cat
printf 'first\n' |& grep -c merged
{ echo grouped-merge; } |& cat
trap - DEBUG

echo pipeline-prefix-assignment
trap 'echo D-$BASH_COMMAND' DEBUG
prefix_stage=one printf '%s\n' prefixed | cat
prefix_stage=two printf '%s\n' second | prefix_stage=three cat
trap - DEBUG
printf 'prefix-leak=%s\n' "${prefix_stage-unset}"

echo pipeline-indirect-word
indirect_command=printf
trap 'echo D-$BASH_COMMAND' DEBUG
$indirect_command '%s\n' indirect | cat
"$indirect_command" '%s\n' quoted | cat
trap - DEBUG

echo pipeline-unresolved
trap 'echo D-$BASH_COMMAND' DEBUG
kosh_absent_stage_command | cat
echo unresolved-first=$?
printf 'a\n' | kosh_absent_stage_command
echo unresolved-last=$?
trap - DEBUG

echo pipeline-negated
trap 'echo D-$BASH_COMMAND' DEBUG
! printf '%s\n' negated | grep -q missing
echo negated-status=$?
trap - DEBUG

echo pipeline-async
trap 'echo D-$BASH_COMMAND' DEBUG
pipeline_function | wc -l > /dev/null &
wait
echo async-status=$?
trap - DEBUG

echo pipeline-errexit
trap 'echo D-$BASH_COMMAND' DEBUG
set -e
printf 'a\n' | grep -q nomatch || echo errexit-guarded
printf 'a\n' | grep -q a
echo errexit-survived
set +e
trap - DEBUG

echo pipeline-pipefail
set -o pipefail
trap 'echo D-$BASH_COMMAND' DEBUG
false | true
echo pipefail-status=$?
printf 'p\n' | grep -q p
echo pipefail-ok=$?
trap - DEBUG
set +o pipefail

echo pipeline-status-array
trap 'echo D-$BASH_COMMAND' DEBUG
false | true
echo pipestatus-two=${PIPESTATUS[*]}
printf 'a\n' | grep -q nomatch | cat
echo pipestatus-three=${PIPESTATUS[*]}
trap - DEBUG
false | true
echo pipestatus-untrapped=${PIPESTATUS[*]}

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
printf 'grouped\n' | { read pipeline_group_value; }
printf 'fnpipe\n' | pipeline_function
trap - DEBUG
printf 'value=%s group=%s\n' "$pipeline_value" "$pipeline_group_value"
shopt -u lastpipe

echo done
