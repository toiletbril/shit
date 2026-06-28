#!/bin/sh
# The read builtin and heredocs, checked against dash. The read operand
# indexing, its honoring of a redirected input descriptor, and the heredoc
# body storage all changed, so this guards them.

read first second rest <<HD
alpha beta gamma delta
HD
echo "first=$first"
echo "second=$second"
echo "rest=$rest"

# A single name takes the whole line.
read whole <<HD
one line only
HD
echo "whole=$whole"

# Multiple heredocs in one script each keep their own body.
cat <<ONE
body one
ONE
cat <<TWO
body two
TWO

# A heredoc that strips leading tabs with the dash form.
cat <<-END
	tab stripped
END

# A bare exec applies its redirections to the shell itself and keeps them for
# every later command. An arbitrary descriptor such as 5 opens a file, takes
# writes across separate commands, appends, and closes, matching dash.
ef=/tmp/shit_exec_fd_test_$$
rm -f "$ef"
exec 5>"$ef"
echo "fd5 line one" >&5
echo "fd5 line two" >&5
exec 5>&-
cat "$ef"

rm -f "$ef"
exec 6>>"$ef"
echo "appended" >&6
exec 6>>"$ef"
echo "still appended" >&6
exec 6>&-
cat "$ef"
rm -f "$ef"

# A duplication onto an arbitrary descriptor copies the current standard output,
# so a later write to it reaches the same place.
exec 7>&1
echo "via fd seven" >&7
exec 7>&-
