#!/bin/bash
# A signal trap does not fire inside a command substitution, checked against
# bash. The inherited action stays in the listing there until a trap command
# changes something, and that command discards every inherited action at once.
chld_count=0
trap 'chld_count=$((chld_count+1))' CHLD

/bin/echo warm > /dev/null
echo "before_sub=$chld_count"

captured=$(/bin/echo one > /dev/null; /bin/echo two > /dev/null; echo body)
echo "captured=[$captured]"
echo "after_sub=$chld_count"

trap 'echo noisy_chld' CHLD
/bin/echo settle > /dev/null
noisy=$(/bin/echo three > /dev/null; /bin/echo four > /dev/null; echo quiet)
echo "noisy=[$noisy]"
trap 'chld_count=$((chld_count+1))' CHLD

/bin/echo three > /dev/null
echo "after_three=$chld_count"
trap - CHLD

trap 'echo fired_hup' HUP
trap 'echo fired_term' TERM

echo "--- a bare listing keeps the inherited actions"
listed=$(trap -p)
echo "$listed"

echo "--- an install discards them"
installed=$(trap 'echo fired_usr1' USR1; trap -p)
echo "$installed"

echo "--- a reset discards them"
cleared=$(trap - HUP; trap -p)
echo "cleared=[$cleared]"

echo "--- an ignore keeps only itself"
ignored=$(trap '' USR2; trap -p)
echo "$ignored"

echo "--- the parent keeps its own"
trap -p
trap - HUP TERM

echo "--- a backquoted substitution"
noise_count=0
trap 'noise_count=$((noise_count+1))' CHLD
/bin/echo settle > /dev/null
old=`/bin/echo four > /dev/null; echo backquote`
echo "old=[$old]"
trap - CHLD

echo done
