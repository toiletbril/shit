#!/bin/sh
# A compound command as a pipeline stage, checked against dash.

# A subshell or a brace group feeds the next stage.
(echo hi) | cat
echo x | { cat; }
{ echo grouped; } | cat

# A subshell that prints several lines pipes its whole output.
(echo a; echo b) | wc -l

# A while-read on the right of a pipe consumes the producer.
printf 'a\nb\nc\n' | while read l; do echo "got $l"; done

# A brace group reads several lines from the pipe.
seq 1 3 | { read a; read b; echo "$a-$b"; }

# The stage runs in a subshell, so its variable changes do not escape.
x=1
printf 'one\ntwo\n' | while read l; do x=$l; done
echo "x=$x"

# The pipeline status is the last stage's status.
(echo a) | false
echo "status=$?"

# A negated pipeline with a compound stage inverts the status.
! (echo a) | grep -q zzz
echo "negated=$?"

# A compound stage in the middle of a longer pipeline still wires through.
seq 1 5 | { while read n; do echo "n=$n"; done; } | wc -l

# A downstream stage that exits early frees a compound or function producer,
# so a large producer behind a subshell or a function does not deadlock.
seq 1 100000 | (cat) | head -3
count_up() { seq 1 100000; }
count_up | cat | head -2
