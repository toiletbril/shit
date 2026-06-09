#!/bin/bash
# Bash type -a for a builtin-only name and type -t for a builtin, keywords, a
# function, and a name with no command, checked byte-for-byte against bash.
type -a cd
type -t cd
type -t if
type -t while
type -t for
type -t case
greet() { :; }
type -t greet
type -t no_such_command_zzz
echo "rc=$?"
