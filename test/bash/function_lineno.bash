#!/bin/bash

# A function body reports the line it is written on inside its own defining
# file. The call site reports the line of the call. Each pair is printed
# together, and no body line matches the call line that reached it.

. "${BASH_SOURCE[0]%/*}/goldens/function_lineno_inner.bash"

echo sourced-definitions
echo "call at $LINENO"
first_line_function
echo "call at $LINENO"
later_function

echo local-definition
local_function() {
  echo "local at $LINENO"
}

echo "call at $LINENO"
local_function

echo nested-call
outer_function() {
  echo "outer at $LINENO"
  local_function
}

echo "call at $LINENO"
outer_function

echo function-lineno-done
