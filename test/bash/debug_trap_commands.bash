#!/bin/bash
# shellcheck disable=SC2034,SC2050,SC2086,SC2157,SC2194,SC2249

echo assignment
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
plain_value=one
appended_value=first
appended_value+=second
trap - DEBUG

echo for-header
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
for item in a b; do
  echo body-$item
done
trap - DEBUG

echo for-positional
set -- x y
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
for item; do
  echo positional-$item
done
trap - DEBUG
set --

echo case-header
subject=match
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
case $subject in
  match) echo matched ;;
  *) echo other ;;
esac
case "$subject" in
  match) echo quoted-matched ;;
esac
case  $subject  in
  match) echo spaced-matched ;;
esac
case $(  echo match  ) in
  match) echo substituted-matched ;;
esac
case "a   b" in
  "a   b") echo quoted-blanks-matched ;;
esac
trap - DEBUG

echo word-loop-header
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
for   word   in   one    two
do
  echo word-$word
done
for word in $(  echo three  ); do
  echo word-$word
done
trap - DEBUG

echo redirection-target
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
echo target > $(  echo /dev/null  )
trap - DEBUG

echo cstyle-header
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
for ((index = 0; index < 2; index++)); do
  echo step-$index
done
trap - DEBUG

echo cstyle-blank-clauses
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
for ((;;)); do break; done
index=0
for ((; index < 2; index++)); do echo blank-init-$index; done
for ((index = 0; ; index++)); do break; done
for ((index = 0; index < 1;)); do break; done
trap - DEBUG

echo arithmetic-command
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
(( 1 + 1 ))
((2 - 2))
trap - DEBUG

echo conditional-command
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
[[ -n x ]]
[[ x = x && -n y ]]
trap - DEBUG

echo branch-headers
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
if true; then echo if-body; fi
if false; then echo skipped; elif true; then echo elif-body; fi
if false; then echo skipped; else echo else-body; fi
trap - DEBUG

echo loop-headers
loop_index=0
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
while [ $loop_index -lt 2 ]; do loop_index=$((loop_index + 1)); done
until [ $loop_index -ge 3 ]; do loop_index=$((loop_index + 1)); done
trap - DEBUG

echo pipeline-commands
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
printf 'payload\n' | wc -l | tr -d ' '
trap - DEBUG

echo pipeline-function
pipeline_function() {
  printf 'payload\n'
}
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
pipeline_function | wc -l | tr -d ' '
trap - DEBUG

echo pipeline-function-functrace
set -T
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
pipeline_function | wc -l | tr -d ' '
pipeline_function | cat
trap - DEBUG
set +T

echo pipeline-trap-output
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
printf 'payload\n' | cat | wc -l | tr -d ' '
trap - DEBUG

echo pipeline-compound-stages
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
{ echo group-a; echo group-b; } | wc -l | tr -d ' '
( echo subshell-stage ) | cat
if true; then echo if-stage; fi | wc -l | tr -d ' '
while false; do :; done | cat
for word in a b; do echo for-$word; done | wc -l | tr -d ' '
echo nested | cat | { cat; } | wc -l | tr -d ' '
trap - DEBUG

echo pipeline-stderr-merge
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
printf 'merged\n' |& cat
printf 'first\n' |& grep -c merged
{ echo grouped-merge; } |& cat
trap - DEBUG

echo pipeline-prefix-assignment
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
prefix_stage=one printf '%s\n' prefixed | cat
prefix_stage=two printf '%s\n' second | prefix_stage=three cat
trap - DEBUG
printf 'prefix-leak=%s\n' "${prefix_stage-unset}"

echo pipeline-indirect-word
indirect_command=printf
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
$indirect_command '%s\n' indirect | cat
"$indirect_command" '%s\n' quoted | cat
trap - DEBUG

echo pipeline-unresolved
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
kosh_absent_stage_command | cat
echo unresolved-first=$?
printf 'a\n' | kosh_absent_stage_command
echo unresolved-last=$?
trap - DEBUG

echo pipeline-negated
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
! printf '%s\n' negated | grep -q missing
echo negated-status=$?
trap - DEBUG

echo pipeline-async
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
pipeline_function | wc -l | tr -d ' ' &
wait
echo async-status=$?
trap - DEBUG

echo pipeline-errexit
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
set -e
printf 'a\n' | grep -q nomatch || echo errexit-guarded
printf 'a\n' | grep -q a
echo errexit-survived
set +e
trap - DEBUG

echo pipeline-pipefail
set -o pipefail
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
false | true
echo pipefail-status=$?
trap - DEBUG
set +o pipefail

echo pipeline-status-array
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
false | true
echo pipestatus-two=${PIPESTATUS[*]}
printf 'a\n' | grep -q nomatch | cat
echo pipestatus-three=${PIPESTATUS[*]}
trap - DEBUG
false | true
echo pipestatus-untrapped=${PIPESTATUS[*]}

echo failing-action-status
( trap 'false' DEBUG
  true
  echo after-true=$?
  ( exit 3 )
  echo after-subshell=$?
  trap - DEBUG )
echo after-failing-action-status=$?

echo failing-action-pipestatus
( trap 'false' DEBUG
  true | false | true
  echo "after-pipeline=$? pipestatus=${PIPESTATUS[*]}"
  trap - DEBUG )
echo after-failing-action-pipestatus=$?

echo action-pipeline-pipestatus
( trap 'echo A | grep -q B' DEBUG
  false | true | false
  echo "after-pipeline=$? pipestatus=${PIPESTATUS[*]}"
  trap - DEBUG )
echo after-action-pipeline-pipestatus=$?

echo pipeline-mutation
pipeline_value=before
pipeline_debug_count=0
trap 'pipeline_value=after; pipeline_debug_count=$((pipeline_debug_count + 1)); echo "D-[$BASH_COMMAND]"' DEBUG
printf '%s\n' "$pipeline_value" | grep -c '^after$'
trap - DEBUG
printf 'value=%s count=%s\n' "$pipeline_value" "$pipeline_debug_count"

echo pipeline-lastpipe
set +m
shopt -s lastpipe
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
printf 'lastpipe\n' | read pipeline_value
printf 'grouped\n' | { read pipeline_group_value; }
printf 'fnpipe\n' | pipeline_function
trap - DEBUG
printf 'value=%s group=%s\n' "$pipeline_value" "$pipeline_group_value"
shopt -u lastpipe

echo pipeline-compound-functrace
set -T
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
{ echo group-a; echo group-b; } | wc -l | tr -d ' '
{ echo group-a; echo group-b; } | cat
( echo subshell-stage ) | cat
for word in a b; do echo for-$word; done | wc -l | tr -d ' '
trap - DEBUG
set +T

echo pipeline-lastpipe-prepared
set +m
shopt -s lastpipe
set -T
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
printf 'lastpipe-traced\n' | read pipeline_traced_value
trap - DEBUG
set +T
shopt -u lastpipe
printf 'traced=%s\n' "$pipeline_traced_value"

echo pipeline-stage-command-text
printf 'x\n' | printf 'stage=[%s]\n' "$BASH_COMMAND"
printf 'y\n' | { printf 'group=[%s]\n' "$BASH_COMMAND"; }

echo pipeline-heredoc-stage
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
cat <<'HEREDOC' | wc -l | tr -d ' '
heredoc line one
heredoc line two
HEREDOC
printf 'piped\n' | cat <<'TAIL'
tail body
TAIL
trap - DEBUG

# Under the extdebug option a nonzero DEBUG action status skips the traced
# command. A skipped command reports success.
echo extdebug-skip
skipping_function() {
  echo skipme
  echo inner-status=$?
  echo function-tail
}
shopt -s extdebug
set -T
trap '[ "$BASH_COMMAND" != "echo skipme" ]' DEBUG
echo kept
echo skipme
echo skipped-status=$?
skipping_function
echo after-function=$?
trap - DEBUG
set +T
shopt -u extdebug
echo extdebug-skip-done

echo extdebug-off
set -T
trap '[ "$BASH_COMMAND" != "echo skipme" ]' DEBUG
echo skipme
echo off-status=$?
trap - DEBUG
set +T
echo extdebug-off-done

# A refused word loop header skips one iteration. The header fires once for
# each value, no body runs, and the loop reports the action status.
echo extdebug-loop-header
shopt -s extdebug
set -T
trap 'echo "D-[$BASH_COMMAND]"; [[ $BASH_COMMAND != for* ]]' DEBUG
for item in a b c; do
  echo header-body-$item
done
echo header-status=$?
set -- p q
for positional; do
  echo positional-body-$positional
done
echo positional-status=$?
trap - DEBUG
set +T
shopt -u extdebug
echo extdebug-loop-header-done

# A refused body command leaves the loop running.
echo extdebug-loop-body
shopt -s extdebug
set -T
trap '[ "$BASH_COMMAND" != "echo skipme" ]' DEBUG
for item in a b; do
  echo skipme
  echo loop-body-$item
done
echo loop-body-status=$?
trap - DEBUG
set +T
shopt -u extdebug
echo extdebug-loop-body-done

# The eval builtin is traced, and every command its argument parses into is
# traced again inside it.
echo eval-command
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
eval 'echo evaluated'
eval "echo double"; eval 'a=1; echo joined-$a'
trap - DEBUG
echo eval-command-done

# A function definition command is not traced. The call that follows it is.
echo function-definition
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
defined_here() {
  echo defined-body
}
function keyworded_here { echo keyworded-body; }
defined_here
keyworded_here
trap - DEBUG
echo function-definition-done

# The time keyword is not traced. The command it measures is.
echo time-keyword
trap 'echo "D-[$BASH_COMMAND]"' DEBUG
time echo timed
trap - DEBUG
echo time-keyword-done

# An action reads the status of the command before the traced one.
echo action-status
saved_status=0
trap 'saved_status=$?; echo "S-$saved_status-[$BASH_COMMAND]"' DEBUG
true
false
(exit 42)
trap - DEBUG
echo action-status=$saved_status
echo action-status-done

echo done
