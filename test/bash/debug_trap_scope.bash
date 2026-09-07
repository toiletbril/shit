#!/bin/bash

body_function() {
  echo in-function
  echo second-line
}

nested_function() {
  body_function
}

echo untraced-function
trap 'echo D-$BASH_COMMAND' DEBUG
body_function
echo after-call
trap - DEBUG

echo nested-function
trap 'echo D-$BASH_COMMAND' DEBUG
nested_function
trap - DEBUG

echo traced-function
set -T
trap 'echo D-$BASH_COMMAND' DEBUG
body_function
trap - DEBUG
set +T
echo after-traced

echo subshell
trap 'echo D-$BASH_COMMAND' DEBUG
( echo in-subshell )
echo after-subshell
trap - DEBUG

echo substitution
trap 'echo D-$BASH_COMMAND' DEBUG
capture_function() {
  echo in-substitution
}
# shellcheck disable=SC2046
echo captured $(capture_function)
trap - DEBUG

echo pinned-command
trap 'echo D-$BASH_COMMAND; echo still-$BASH_COMMAND' DEBUG
echo target
trap - DEBUG

echo exit-command
trap 'echo E-$BASH_COMMAND' EXIT
echo last-command
