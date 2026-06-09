#!/bin/bash
echo "empty: $(( ))"
n=(a b c)
echo "unset-sub: ${n[$1]}"
echo "expr-empty: $(( ${#nosuchvar} ))"
type -at if
f() { :; }
type -at f
alias_target_builtin() { :; }
type -t type
