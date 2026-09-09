#!/bin/bash
# A CHLD action that jumps out of the frame it fired inside, checked against
# bash. The arrivals the jump left behind stay queued, and an exit the action
# runs ends the shell with the status it names.
fire_count=0

echo "--- a break leaves the loop the action fired inside"
trap 'fire_count=$((fire_count+1)); echo "fire=$fire_count"; break' CHLD
for word in one two three; do
  /bin/echo "$word" > /dev/null
  echo "body=$word"
done
echo "after_break=$fire_count"
trap - CHLD

echo "--- a continue skips the rest of the body"
fire_count=0
trap 'fire_count=$((fire_count+1)); echo "fire=$fire_count"; continue' CHLD
for word in one two three; do
  /bin/echo "$word" > /dev/null
  echo "body=$word"
done
echo "after_continue=$fire_count"
trap - CHLD

echo "--- a break with no enclosing loop is reported"
trap 'echo breaking; break' CHLD
/bin/echo loose > /dev/null
echo "after_loose=$?"
trap - CHLD

echo "--- two arrivals in a row are both counted"
fire_count=0
trap 'fire_count=$((fire_count+1)); echo "queued=$fire_count"' CHLD
/bin/echo first > /dev/null
/bin/echo second > /dev/null
echo "after_two=$fire_count"
trap - CHLD

echo "--- a normal action keeps the triggering status"
trap 'echo normal' CHLD
( exit 9 )
echo "after_normal=$?"
trap - CHLD

echo "--- a subshell leaves with the operand its action gave"
( trap 'echo sub_fired; exit 7' CHLD
  /bin/echo sub > /dev/null
  echo sub_tail )
echo "sub_status=$?"

echo "--- the shell leaves with the status the action names"
trap 'echo leaving; exit 7' CHLD
/bin/echo last > /dev/null
echo unreachable
