#!/bin/bash
# declare -F answers function existence by status and prints bare names, the
# bare form lists declare -f lines, and a cloned definition evals back.
f() { echo body; }
function g/h:i { echo colon; }
declare -F f; echo "s=$?"
declare -F nope 2>/dev/null; echo "missing=$?"
declare -F f nope 2>/dev/null; echo "multi=$?"
declare -F -- g/h:i >/dev/null; echo "bleform=$?"
declare -F | grep -c 'declare -f f'
typeset -F f
def=$(declare -f g/h:i)
new=${def/#"g/h:i"/"clone:x"}
eval "$new"
clone:x
echo "clone=$?"
declare -f nope2 2>/dev/null; echo "deff_missing=$?"
