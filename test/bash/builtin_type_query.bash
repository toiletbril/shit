#!/bin/bash
# Bash type -a for cd and type -t for a builtin, keywords, a function, and a name
# with no command, checked byte-for-byte against bash. PATH is pinned to a single
# directory so the type -a cd listing is deterministic. A usrmerge system carries
# /usr/sbin as a symlink to /usr/bin, so a PATH with both makes bash list the cd
# binary twice while shit lists it once, a divergence the single entry removes.
PATH=/usr/bin
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
