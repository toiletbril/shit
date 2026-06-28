#!/bin/bash
# Bash &>, &>>, and |& redirections, checked byte-for-byte against bash. The
# &> forms write to a temp file that is read back, the |& form pipes both
# standard streams so the result is observable on standard output.
tmp=/tmp/shit_bashdiff_ampredir_$$
echo hello &>"$tmp"
cat "$tmp"
{ echo out; echo err >&2; } &>"$tmp"
sort "$tmp"
echo first >"$tmp"
echo second &>>"$tmp"
sort "$tmp"
rm -f "$tmp"
{ echo o; echo e >&2; } |& sort
ls /nonexistent_path_xyz |& grep -o "No such"
echo piped |& cat
printf 'x\ny\nz\n' |& wc -l | tr -d ' '
