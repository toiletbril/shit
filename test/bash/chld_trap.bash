#!/bin/bash
# The CHLD trap, checked against bash. The action runs once for every child the
# shell reaps. A pipeline of two stages fires it twice, and two background jobs
# fire it twice.
trap 'chld_count=$((chld_count+1))' CHLD

chld_count=0
/bin/echo one > /dev/null
echo "single=$chld_count"

chld_count=0
/bin/echo a | /bin/cat > /dev/null
echo "pipeline=$chld_count"

chld_count=0
/bin/echo x | /bin/cat | /bin/cat > /dev/null
echo "three_stage=$chld_count"

chld_count=0
substituted=$(/bin/echo captured)
echo "substitution=$chld_count value=$substituted"

chld_count=0
/bin/sleep 0.05 &
/bin/sleep 0.05 &
wait
echo "background=$chld_count"

report() {
  chld_count=0
  /bin/echo nested > /dev/null
  echo "in_function=$chld_count"
}
report

chld_count=0
for word in one two three; do
  /bin/echo "$word" > /dev/null
done
echo "loop=$chld_count"

chld_count=0
( /bin/echo sub > /dev/null )
echo "subshell=$chld_count"

chld_count=0
/bin/sleep 0.05 &
wait $!
echo "waited=$chld_count"

chld_count=0
/bin/cat /nonexistent-kosh-probe 2> /dev/null
echo "failed=$chld_count"

chld_count=0
{ /bin/echo group > /dev/null ; }
echo "group=$chld_count"

trap - CHLD
chld_count=0
/bin/echo removed > /dev/null
echo "after_removal=$chld_count"
