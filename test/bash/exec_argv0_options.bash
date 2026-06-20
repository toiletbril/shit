#!/bin/bash
# exec reads -a, -l, and the -- terminator before the command word, the way bash
# does. -a names the zeroth argument the program reads, -l prefixes that argument
# with a dash so the program reads itself as a login shell, and the two combine
# in a cluster. Each exec runs inside a command substitution, so it replaces the
# contained child rather than the script, and the script survives to print every
# case. The zeroth argument is read back from /proc, and both shells read the
# same place so the comparison holds where /proc exists.
echo "plain=$(exec -a foo cat /proc/self/cmdline 2>/dev/null | tr '\0' ' ')"
echo "login=$(exec -a foo -l cat /proc/self/cmdline 2>/dev/null | tr '\0' ' ')"
echo "cluster=$(exec -la foo cat /proc/self/cmdline 2>/dev/null | tr '\0' ' ')"
echo "attached=$(exec -afoo cat /proc/self/cmdline 2>/dev/null | tr '\0' ' ')"
echo "terminator=$(exec -- cat /proc/self/cmdline 2>/dev/null | tr '\0' ' ')"
echo "alive=yes"
