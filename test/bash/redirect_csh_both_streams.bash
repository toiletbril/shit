#!/bin/bash
# The csh both-streams spelling cmd >&file redirects stdout and stderr to the
# file when the word names no descriptor, while a numeric word keeps the
# descriptor duplication and an explicit fd keeps the strict reading.
out=$(mktemp)
echo visible >&"$out"
ls /nonexistent_zzqq >>"$out" 2>&1
grep -c visible "$out"
grep -c nonexistent "$out"
echo hi >&/dev/null
echo after
exec 3>&1
echo fd3 >&3
exec 3>&-
rm -f "$out"
