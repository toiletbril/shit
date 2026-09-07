#!/bin/bash

. bash/goldens/function_lineno_inner.bash

echo sourced-definitions
first_line_function
later_function

echo local-definition
local_function() {
  echo "local at $LINENO"
}
local_function
