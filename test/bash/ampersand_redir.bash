#!/bin/bash
# Bash &>, &>>, and |& redirections, checked byte-for-byte against bash. The
# &> forms write to a temp file that is read back, the |& form pipes both
# standard streams so the result is observable on standard output.
tmp=/tmp/kosh_bashdiff_ampredir_$$
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


# Dynamic file descriptor allocation, the exec {var}>file form and its dup,
# read, close, and compound spellings, checked byte for byte against bash. The
# allocated number depends on the descriptors the harness leaves open, so the
# checks assert the descriptor lands at or above ten and the data flows rather
# than the absolute number, and a brace word with no adjacent redirect stays an
# argument.
tmp=/tmp/kosh_bashdiff_fdalloc_$$

exec {w}>"$tmp"
echo "w>=10: $(( w >= 10 ))"
printf 'line one\n' >&$w
printf 'line two\n' >&$w
exec {w}>&-
cat "$tmp"

exec {dup}>&1
echo "dup>=10: $(( dup >= 10 ))"
printf 'through the dup\n' >&$dup
exec {dup}>&-

printf 'a\nb\n' >"$tmp"
exec {r}<"$tmp"
read first <&$r
read second <&$r
echo "read $first $second"
exec {r}<&-

exec {one}>/dev/null
exec {two}>/dev/null
echo "two distinct: $(( one != two && one >= 10 && two >= 10 ))"
exec {one}>&-
exec {two}>&-

{ echo grouped; } {g}>"$tmp"
echo "g>=10: $(( g >= 10 ))"
cat "$tmp"

echo literal {brace} stays a word

rm -f "$tmp"

# Bash here-string <<<, checked byte-for-byte against bash. Feeds the expanded
# word plus a newline as standard input. Covers a literal, a variable, an empty
# string, piping into a builtin, and a read into a variable.
cat <<< "hello world"
wc -c <<< "abc" | tr -d ' '
v=expanded
cat <<< "$v"
read first rest <<< "one two three"
echo "$first | $rest"
grep -o match <<< "a match here"
rev <<< "stressed"
tr 'a-z' 'A-Z' <<< "lower"
n=42
cat <<< "the number is $n"
while read line; do echo "line: $line"; done <<< "only one"
cat <<< ""
wc -l <<< "no newline added beyond one" | tr -d ' '

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

# A redirection that cannot open its target fails the command with status 1 in
# the bash mood and the shell continues to the next command, checked against
# bash. The POSIX mood reports the same failure as status 2, covered under the
# dash comparison.
cat < /no/such/directory/file 2>/dev/null
echo "read_status=$?"
echo > /no/such/directory/file 2>/dev/null
echo "write_status=$?"
echo still_running

# A pipeline stage whose command does not resolve applies its own redirections
# before its diagnostic is written, so the stage decides where the message
# lands. The message text differs between the shells, the checks assert the
# routing and the status.
report=/tmp/kosh_bashdiff_unresolved_$$
: > "$report"
{ echo x | nosuchcmd_zzqq 2>/dev/null; } 2>"$report"
echo "hidden_status=$? hidden_leak=$(( $(wc -c < "$report") > 0 ))"
echo x | nosuchcmd_zzqq 2>"$report"
echo "captured_status=$? captured=$(( $(wc -c < "$report") > 0 ))"
echo x | nosuchcmd_zzqq >"$report" 2>&1
echo "merged_status=$? merged=$(( $(wc -c < "$report") > 0 ))"
: > "$report"
echo x | nosuchcmd_zzqq &>"$report"
echo "both_status=$? both=$(( $(wc -c < "$report") > 0 ))"
: > "$report"
{ nosuchcmd_zzqq 2>/dev/null | cat; } 2>"$report"
echo "first_hidden_status=$? first_hidden_leak=$(( $(wc -c < "$report") > 0 ))"
nosuchcmd_zzqq 2>"$report" | cat
echo "first_captured_status=$? first_captured=$(( $(wc -c < "$report") > 0 ))"
# A stage that merges its error into its output carries the diagnostic onto the
# pipe, and the last stage of such a pipeline carries it onto the shell's own
# standard output. The message text differs between the shells, the checks
# assert that the bytes arrive at the merged destination.
piped=$(nosuchcmd_zzqq 2>&1 | wc -c | tr -d ' ')
echo "piped_merged=$(( piped > 0 ))"
piped_hidden=$(nosuchcmd_zzqq 2>/dev/null | wc -c | tr -d ' ')
echo "piped_hidden=$piped_hidden"
( echo x | nosuchcmd_zzqq 2>&1 ) >"$report" 2>/dev/null
echo "last_merged=$(( $(wc -c < "$report") > 0 ))"
rm -f "$report"
echo unresolved_done

# Each redirection on a stage applies where the source writes it. A dup written
# before the redirection of its source descriptor keeps the stream the stage
# inherits, and the file that follows moves only its own descriptor. The
# surrounding subshell captures the inherited stream so both destinations are
# observable.
ordered=/tmp/kosh_bashdiff_ordered_$$
ordered_out=/tmp/kosh_bashdiff_ordered_out_$$
( echo x | /bin/sh -c 'echo E >&2; echo O' 2>&1 >"$ordered" ) >"$ordered_out"
echo "ordered_file=[$(cat "$ordered")] ordered_inherited=[$(cat "$ordered_out")]"
( echo x | /bin/sh -c 'echo E >&2; echo O' >"$ordered" 2>&1 ) >"$ordered_out"
echo "merged_file=[$(sort "$ordered" | tr '\n' ' ')] merged_inherited=[$(cat "$ordered_out")]"
( /bin/sh -c 'echo E >&2; echo O' 1>&2 2>"$ordered" ) 2>"$ordered_out"
echo "swapped_file=[$(cat "$ordered")] swapped_inherited=[$(cat "$ordered_out")]"

# The write end of a pipe is the stage's standard output before the stage
# applies its own redirections. A 2>&1 written ahead of the file on the stage
# carries the standard error onto the pipe, and a 1>&2 written ahead of the file
# replaces the stage's standard output with the inherited standard error, which
# leaves the pipe with nothing to carry.
ordered_err=/tmp/kosh_bashdiff_ordered_err_$$
( /bin/sh -c 'echo E >&2; echo O' 2>&1 >"$ordered" | sed 's/^/P:/' ) >"$ordered_out"
echo "pipe_dup_file=[$(tr '\n' ' ' < "$ordered")] pipe_dup_piped=[$(tr '\n' ' ' < "$ordered_out")]"
( /bin/sh -c 'echo E >&2; echo O' 1>&2 2>"$ordered" | sed 's/^/P:/' ) >"$ordered_out" 2>"$ordered_err"
echo "pipe_swap_file=[$(tr '\n' ' ' < "$ordered")] pipe_swap_inherited=[$(tr '\n' ' ' < "$ordered_err")] pipe_swap_piped=[$(tr '\n' ' ' < "$ordered_out")]"
rm -f "$ordered" "$ordered_out" "$ordered_err"
echo ordered_done
