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
# A diagnostic merged onto the pipe can be larger than the pipe buffer, and the
# reading stage is launched after the stage that fails to resolve. The report
# waits for every stage, so the reader is already draining the pipe.
long=x
long_step=0
while [ "$long_step" -lt 15 ]; do
  long=$long$long
  long_step=$(( long_step + 1 ))
done
long_bytes=$("$long" 2>&1 | wc -c | tr -d ' ')
echo "long_merged=$(( long_bytes > 0 ))"
# The reading stage is a group, which the shell runs in a forked child. That
# child keeps the descriptors of a stage that has not reported yet, so the read
# ends only after the report releases them.
reader_line=$(nosuchcmd_zzqq 2>&1 | { read line; echo "$line"; })
echo "group_reader=$(( ${#reader_line} > 0 ))"
long_reader=$("$long" 2>&1 | { cat; })
echo "long_group_reader=$(( ${#long_reader} > 0 ))"
echo unresolved_done

# A builtin runs inside the shell and writes its own diagnostic through the
# descriptor its redirections leave on the shell. The message text differs
# between the shells, the checks assert the length and the status.
builtin_report=/tmp/kosh_bashdiff_builtin_$$
: > "$builtin_report"
builtin_captured_text=$( { cd /nonexistent_zzqq 2>&1; } 2>/dev/null )
echo "builtin_merged=$(( ${#builtin_captured_text} > 0 ))"
cd /nonexistent_zzqq 2>"$builtin_report"
echo "builtin_status=$? builtin_captured=$(( $(wc -c < "$builtin_report") > 0 ))"
: > "$builtin_report"
cd /nonexistent_zzqq 2>/dev/null
echo "builtin_hidden_status=$? builtin_hidden=$(( $(wc -c < "$builtin_report") > 0 ))"
unset -x 2>"$builtin_report"
echo "unset_status=$? unset_captured=$(( $(wc -c < "$builtin_report") > 0 ))"
: > "$builtin_report"
read -Z 2>"$builtin_report" </dev/null
echo "read_flag_status=$? read_flag_captured=$(( $(wc -c < "$builtin_report") > 0 ))"
rm -f "$builtin_report"
echo builtin_report_done

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

# A second file on a descriptor whose dup already read the first one leaves that
# first file open as the other stream, so the two files receive one stream each.
( /bin/sh -c 'echo E >&2; echo O' >"$ordered" 2>&1 >"$ordered_out" | sed 's/^/P:/' ) >/dev/null
echo "repeat_first=[$(tr '\n' ' ' < "$ordered")] repeat_second=[$(tr '\n' ' ' < "$ordered_out")]"
( /bin/sh -c 'echo E >&2; echo O' 2>"$ordered" 1>&2 2>"$ordered_out" | sed 's/^/P:/' ) >/dev/null
echo "swap_first=[$(tr '\n' ' ' < "$ordered")] swap_second=[$(tr '\n' ' ' < "$ordered_out")]"
rm -f "$ordered" "$ordered_out" "$ordered_err"
echo ordered_done

# A stage redirection whose target is a descriptor above two keeps its own
# number. The pipe stays on the standard output of the stage, and the file, the
# duplication, and the close each reach the descriptor the source names.
nonstd=/tmp/kosh_bashdiff_nonstd_$$
nonstd_in=/tmp/kosh_bashdiff_nonstd_in_$$
/bin/sh -c 'echo deep >&3' 3>"$nonstd" | cat
echo "nonstd_file=[$(cat "$nonstd")]"
echo piped_beside 3>"$nonstd" | cat
echo "nonstd_pipe_kept=[$(cat "$nonstd")]"
printf 'from three\n' > "$nonstd_in"
/bin/sh -c 'read line <&3; echo "$line"' 3<"$nonstd_in" | cat
/bin/sh -c 'echo dup4 >&4' 4>&1 | cat
/bin/sh -c 'echo closed >&3' 3>&- 2>/dev/null | cat
echo "nonstd_closed_status=$?"
# The builtin stage runs inside the shell, so its binding is put back before the
# next command reads the same descriptor.
exec 3>"$nonstd"
echo replaced 3>/dev/null | cat
echo still_mine >&3
exec 3>&-
echo "nonstd_restored=[$(cat "$nonstd")]"
# A standard descriptor of a stage can name a source the shell holds and the
# stage never carries, and it can also be closed for that stage alone.
exec 3>"$nonstd"
echo to_three 1>&3 | cat
exec 3>&-
echo "nonstd_source=[$(cat "$nonstd")]"
printf 'read through three\n' > "$nonstd_in"
exec 3<"$nonstd_in"
read line 0<&3
exec 3<&-
echo "nonstd_read=[$line]"
echo dropped 1>&- 2>/dev/null | cat
echo "nonstd_out_closed_status=$?"
echo kept 2>&- | cat
echo "nonstd_err_closed_status=$?"
rm -f "$nonstd" "$nonstd_in"
echo nonstandard_done

# A pipeline stage whose redirection cannot be applied fails that stage alone.
# The stage keeps the redirections written ahead of the failing one, so its
# diagnostic reaches the destination those redirections named, and the pipeline
# still takes the status of its last stage. The message text differs between the
# shells, the checks assert the statuses, the PIPESTATUS entries, and the
# routing.
echo one 2>/dev/null 1>/nonexistent_zzqq/x | cat
echo "redir_fail_status=$? redir_fail_ps=${PIPESTATUS[*]}"
echo one 2>/dev/null 1>&7 | cat
echo "bad_dup_status=$? bad_dup_ps=${PIPESTATUS[*]}"
true | cat 2>/dev/null 1>/nonexistent_zzqq/x | cat
echo "middle_fail_status=$? middle_fail_ps=${PIPESTATUS[*]}"
cat </dev/null | echo two 2>/dev/null 1>/nonexistent_zzqq/x
echo "last_fail_status=$? last_fail_ps=${PIPESTATUS[*]}"
redir_report=/tmp/kosh_bashdiff_redirfail_$$
: > "$redir_report"
{ echo one 1>/nonexistent_zzqq/x 2>"$redir_report" | cat; } 2>/dev/null
echo "reported_status=$? reported=$(( $(wc -c < "$redir_report") > 0 ))"
: > "$redir_report"
{ echo one 2>&1 1>/nonexistent_zzqq/x | cat; } >"$redir_report" 2>/dev/null
echo "merged_status=$? merged=$(( $(wc -c < "$redir_report") > 0 ))"
: > "$redir_report"
{ echo kept 2>"$redir_report" 1>/nonexistent_zzqq/x | cat; } 2>/dev/null
echo "captured_status=$? captured=$(( $(wc -c < "$redir_report") > 0 ))"
rm -f "$redir_report"
# Every other stage of the pipeline still runs, so the reader observes the data
# the surviving stages write.
echo pre_marker | { cat; echo reader_ran; } 2>/dev/null
echo redir_fail_done
