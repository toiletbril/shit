#!/bin/bash

# An assignment to RANDOM seeds the generator, and a repeated seed repeats the
# sequence. A local declaration turns the name into an ordinary frozen variable
# for the length of the call, and the generator keeps moving underneath it. The
# drawn numbers are the generator's own, so this script asserts the properties
# the two shells share.

echo seeded-sequence
RANDOM=42
first=$RANDOM
second=$RANDOM
RANDOM=42
repeat_first=$RANDOM
repeat_second=$RANDOM
if [ "$first" = "$repeat_first" ] && [ "$second" = "$repeat_second" ]; then
  echo repeated-seed=repeats
else
  echo repeated-seed=diverges
fi

if [ "$first" -ge 0 ] && [ "$first" -le 32767 ]; then
  echo range=within-0-to-32767
else
  echo "range=$first"
fi

RANDOM=1
other=$RANDOM
if [ "$other" != "$first" ]; then
  echo other-seed=differs
else
  echo other-seed=matches
fi
echo seeded-sequence-done

echo text-value
RANDOM=abc
from_text=$RANDOM
RANDOM=0
from_zero=$RANDOM
if [ "$from_text" = "$from_zero" ]; then
  echo text=reads-as-zero
else
  echo text=reads-as-something-else
fi
echo text-value-done

echo declared-form
RANDOM=7
declare -p RANDOM | /usr/bin/sed 's/=.*//'
export RANDOM=7
declare -p RANDOM | /usr/bin/sed 's/=.*//'
export -n RANDOM
echo declared-form-done

# Moving the name into the environment does not take the generator away from
# it. Three draws are compared, because two equal draws are an ordinary result
# of a generator that is still running.
echo exported-value
export RANDOM=7
first_draw=$RANDOM
second_draw=$RANDOM
third_draw=$RANDOM
if [ "$first_draw" = "$second_draw" ] && [ "$second_draw" = "$third_draw" ]; then
  echo exported=frozen
else
  echo exported=keeps-drawing
fi
export -n RANDOM
fourth_draw=$RANDOM
fifth_draw=$RANDOM
sixth_draw=$RANDOM
if [ "$fourth_draw" = "$fifth_draw" ] && [ "$fifth_draw" = "$sixth_draw" ]; then
  echo unexported=frozen
else
  echo unexported=keeps-drawing
fi
echo exported-value-done

echo local-shadow
draw_inside() {
  local RANDOM=9
  echo "entered=$RANDOM"
  echo "again=$RANDOM"
}
RANDOM=3
draw_inside
after_one=$RANDOM
after_two=$RANDOM
if [ "$after_one" != "$after_two" ]; then
  echo outer=keeps-drawing
else
  echo outer=frozen
fi
echo local-shadow-done

echo random-seed-done
