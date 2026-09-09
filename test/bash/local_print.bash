#!/bin/bash
# The local -p listing, checked against bash. A bare local -p prints the
# declaration of every name the running scope declared, in declaration order. A
# name declared without a value still prints, and only the innermost scope is
# listed.
f() {
  local a=1
  local -i b=2
  local c
  local -r d=4
  local -a e=(x y)
  local -x g=7
  local -p
}
f
echo "bare_rc=$?"

nested_inner() {
  local inner_only=2
  local -p
}
nested_outer() {
  local outer_only=1
  nested_inner
}
nested_outer

named() {
  local declared
  local -p declared
  echo "declared_rc=$?"
  local -r fixed
  local -p fixed
  echo "fixed_rc=$?"
  declare -p declared
  echo "declare_rc=$?"
  local -p absent 2>/dev/null
  echo "absent_rc=$?"
}
named

empty_scope() {
  local -p
  echo "empty_rc=$?"
}
empty_scope

redeclared() {
  local r=first
  local r=second
  local -p
}
redeclared
