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

echo exit-in-action-select
( trap 'exit 9' DEBUG; select choice in a b; do echo unreachable-$choice; done ) </dev/null
echo after-exit-in-action-select=$?

echo exit-in-action-blank-init
( index=0; trap 'exit 9' DEBUG; for ((; index < 2; index++)); do echo unreachable-$index; done )
echo after-exit-in-action-blank-init=$?

echo exit-in-action-blank-condition
( trap 'if [[ $BASH_COMMAND == "((1))" ]]; then exit 9; fi' DEBUG; for ((index = 0; ; index++)); do echo unreachable-$index; done )
echo after-exit-in-action-blank-condition=$?

echo exit-in-action-blank-step
( trap 'if [[ $BASH_COMMAND == "((1))" ]]; then exit 9; fi' DEBUG; for ((index = 0; index < 3; )); do echo body-$index; index=$((index + 1)); done )
echo after-exit-in-action-blank-step=$?

echo break-in-action
( trap 'case "$BASH_COMMAND" in echo\ body*) break ;; esac' DEBUG
  for value in 1 2 3; do echo body-$value; echo after-$value; done
  trap - DEBUG
  echo break-tail )
echo after-break-in-action=$?

echo continue-in-action
( trap 'case "$BASH_COMMAND" in echo\ after*) continue ;; esac' DEBUG
  for value in 1 2 3; do echo body-$value; echo after-$value; echo skipped-$value; done
  trap - DEBUG
  echo continue-tail )
echo after-continue-in-action=$?

echo return-in-action
set -T
returning_function() {
  trap 'case "$BASH_COMMAND" in echo\ inner*) return 9 ;; esac' DEBUG
  echo inner-body
  trap - DEBUG
  echo unreachable-return
}
( returning_function
  echo returned=$?
  echo return-tail )
echo after-return-in-action=$?
set +T

echo pinned-command
trap 'echo "D-$BASH_COMMAND"; echo "still-$BASH_COMMAND"' DEBUG
echo target
trap - DEBUG

echo action-no-reentry
( trap 'echo action-once' DEBUG
  echo target-command
  trap - DEBUG )
echo after-action-no-reentry=$?

echo action-nested-commands
( trap 'echo one; echo two; echo three' DEBUG
  echo single-target
  trap - DEBUG )
echo after-action-nested-commands=$?

echo action-calls-function
( reentry_function() { echo in-reentry-function; echo second-in-function; }
  trap 'reentry_function' DEBUG
  echo target-of-function-action
  trap - DEBUG )
echo after-action-calls-function=$?

echo depth-gate-function
( installer_function() {
    trap 'echo "D-$BASH_COMMAND"' DEBUG
    echo at-install-depth
    deeper_function
  }
  deeper_function() { echo in-deeper; }
  installer_function
  echo after-installer )
echo after-depth-gate-function=$?

echo depth-gate-subshell
( trap 'echo "D-$BASH_COMMAND"' DEBUG
  echo at-subshell-depth
  ( echo in-nested-subshell ) )
echo after-depth-gate-subshell=$?

echo depth-gate-substitution
# shellcheck disable=SC2046
( echo captured $(trap 'echo "D-$BASH_COMMAND"' DEBUG
                  echo at-substitution-depth
                  echo nested $(echo in-nested-substitution)) )
echo after-depth-gate-substitution=$?

echo lineno-action-function
( action_reporter() { echo action-function-lineno=$LINENO; }
  trap 'action_reporter' DEBUG
  echo triggering-function
  trap - DEBUG )
echo after-lineno-action-function=$?

echo lineno-action-source
( trap '. bash/goldens/debug_trap_action_source.bash' DEBUG
  echo triggering-source
  trap - DEBUG )
echo after-lineno-action-source=$?

# A command substitution pushes a source frame of its own, so the action that
# fires for a command inside it reports the line of that command.
echo lineno-action-substitution
( trap 'echo "L-$LINENO-[$BASH_COMMAND]"' DEBUG
  echo plain-target
  captured=$(echo substituted-target)
  echo "$captured"
  trap - DEBUG )
echo after-lineno-action-substitution=$?

# An EXIT action a subshell sets keeps the command that triggered it and the
# status that command left, the same way the shell's own EXIT action does.
echo subshell-exit-command
( trap 'echo "S-$?-[$BASH_COMMAND]"' EXIT
  echo in-subshell
  false )
echo after-subshell-exit-command=$?
( trap 'echo "S-$?-[$BASH_COMMAND]"' EXIT
  echo in-subshell-clean )
echo after-subshell-exit-clean=$?

# A loop header fires once for every round, and every one of those fires
# reports the line of the header rather than the last body line.
echo loop-header-lineno
trap 'echo "L-$LINENO-[$BASH_COMMAND]"' DEBUG
for header_value in 1 2; do
  echo word-body-a-$header_value
  echo word-body-b-$header_value
done
trap - DEBUG
echo after-word-loop-header=$?

trap 'echo "L-$LINENO-[$BASH_COMMAND]"' DEBUG
for ((header_index = 0; header_index < 2; header_index++)); do
  echo arith-body-a-$header_index
  echo arith-body-b-$header_index
done
trap - DEBUG
echo after-arithmetic-loop-header=$?

header_counter=0
trap 'echo "L-$LINENO-[$BASH_COMMAND]"' DEBUG
while [ $header_counter -lt 2 ]; do
  echo while-body-$header_counter
  header_counter=$((header_counter + 1))
done
trap - DEBUG
echo after-while-loop-header=$?

# A call that functrace does not trace runs its body without the DEBUG trap the
# caller installed, so the body lists none and a removal it runs takes nothing
# away from the caller. A trap the body installs for itself stands after the
# return, and functrace hands the body the caller's trap to remove.
echo untraced-removal
listing_function() {
  echo "listed: [$(trap -p DEBUG)]"
}
removing_function() {
  trap - DEBUG
  echo in-removing-function
}
replacing_function() {
  trap 'echo "R-$BASH_COMMAND"' DEBUG
  echo in-replacing-function
}
trap 'echo "D-$BASH_COMMAND"' DEBUG
listing_function
removing_function
echo after-removing-function
trap - DEBUG
echo untraced-removal-done

echo untraced-replacement
trap 'echo "D-$BASH_COMMAND"' DEBUG
replacing_function
echo after-replacing-function
trap - DEBUG
echo untraced-replacement-done

echo traced-removal
set -T
trap 'echo "D-$BASH_COMMAND"' DEBUG
removing_function
echo after-traced-removal
trap - DEBUG
set +T
echo traced-removal-done

echo exit-command
trap 'echo "E-$?-[$BASH_COMMAND]"' EXIT
echo last-command
