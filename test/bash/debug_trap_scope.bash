#!/bin/bash

body_function() {
  echo in-function
  echo second-line
}

nested_function() {
  body_function
}

echo untraced-function
trap 'echo "D-$BASH_COMMAND"' DEBUG
body_function
echo after-call
trap - DEBUG

echo nested-function
trap 'echo "D-$BASH_COMMAND"' DEBUG
nested_function
trap - DEBUG

echo traced-function
set -T
trap 'echo "D-$BASH_COMMAND"' DEBUG
body_function
trap - DEBUG
set +T
echo after-traced

echo subshell
trap 'echo "D-$BASH_COMMAND"' DEBUG
( echo in-subshell )
echo after-subshell
trap - DEBUG

echo substitution
trap 'echo "D-$BASH_COMMAND"' DEBUG
capture_function() {
  echo in-substitution
}
# shellcheck disable=SC2046
echo captured $(capture_function)
trap - DEBUG

echo subshell-installed
( trap 'echo "D-$BASH_COMMAND"' DEBUG; echo in-subshell )
echo after-subshell-installed

echo debug-lineno
lineno_body_function() {
  echo in-lineno-function
}
set -T
trap 'echo "L-$LINENO-[$BASH_COMMAND]"' DEBUG
echo first
echo second
lineno_body_function
trap - DEBUG
set +T
echo debug-lineno-done

echo function-installed
installing_function() {
  trap 'echo "D-$BASH_COMMAND"' DEBUG
  echo in-installing-function
}
untraced_function() {
  echo in-untraced-function
}
installing_function
untraced_function
trap - DEBUG

echo traced-function-installed
set -T
installing_function
trap - DEBUG
set +T

echo substitution-installed
# shellcheck disable=SC2046
echo captured $(trap 'echo "D-$BASH_COMMAND"' DEBUG; echo in-substitution)
trap - DEBUG

echo exit-in-action-simple
( trap 'exit 9' DEBUG; echo unreachable-simple )
echo after-exit-in-action-simple=$?

echo exit-in-action-word-loop
( trap 'exit 9' DEBUG; for value in 1 2; do echo unreachable-$value; done )
echo after-exit-in-action-word-loop=$?

echo exit-in-action-case
( trap 'exit 9' DEBUG; case x in x) echo unreachable-case ;; esac )
echo after-exit-in-action-case=$?

echo exit-in-action-arithmetic-loop
( trap 'exit 9' DEBUG; for ((index = 0; index < 2; index++)); do echo unreachable-$index; done )
echo after-exit-in-action-arithmetic-loop=$?

echo exit-in-action-conditional
( trap 'exit 9' DEBUG; [[ -n x ]]; echo unreachable-conditional )
echo after-exit-in-action-conditional=$?

echo exit-in-action-assignment
( trap 'exit 9' DEBUG; assigned=1; echo unreachable-$assigned )
echo after-exit-in-action-assignment=$?

echo exit-in-action-function
set -T
exiting_function() {
  trap 'exit 9' DEBUG
  echo unreachable-function
}
( exiting_function )
echo after-exit-in-action-function=$?
set +T

echo entry-fire-text
entry_body_function() {
  echo in-entry-body
}
set -T
trap 'echo "A-[$BASH_COMMAND]-$LINENO"' DEBUG
entry_body_function
trap - DEBUG
set +T

echo exit-in-action-function-entry
counting_function() {
  echo in-counting-body
}
set -T
( fire_count=0
  trap 'fire_count=$((fire_count + 1)); echo "C-$fire_count-[$BASH_COMMAND]"; if [ $fire_count -ge 2 ]; then exit 9; fi' DEBUG
  counting_function
  echo unreachable-counting-tail )
echo after-exit-in-action-function-entry=$?
set +T

echo exit-in-action-pipeline
( trap 'exit 9' DEBUG; echo unreachable-a | cat; echo unreachable-tail )
echo after-exit-in-action-pipeline=$?

echo exit-in-action-pipeline-three
( trap 'exit 9' DEBUG; echo unreachable-a | cat | cat )
echo after-exit-in-action-pipeline-three=$?

echo exit-in-action-pipeline-compound
( echo before-compound-pipeline; trap 'exit 9' DEBUG; { echo unreachable-a; } | cat )
echo after-exit-in-action-pipeline-compound=$?

echo exit-in-action-pipeline-loop-stage
( echo before-loop-pipeline; trap 'exit 9' DEBUG; for value in 1; do echo unreachable-$value; done | cat )
echo after-exit-in-action-pipeline-loop-stage=$?

echo exit-in-action-pipeline-later-stage
( trap 'if [[ $BASH_COMMAND == cat* ]]; then exit 9; fi' DEBUG; echo unreachable-a | cat | cat )
echo after-exit-in-action-pipeline-later-stage=$?

echo exit-in-action-pipeline-async
( trap 'exit 9' DEBUG; echo unreachable-a | cat & wait )
echo after-exit-in-action-pipeline-async=$?

echo pinned-command
trap 'echo "D-$BASH_COMMAND"; echo "still-$BASH_COMMAND"' DEBUG
echo target
trap - DEBUG

echo exit-command
trap 'echo E-$BASH_COMMAND' EXIT
echo last-command
