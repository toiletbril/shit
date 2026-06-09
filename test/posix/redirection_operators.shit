#!/bin/sh
# The >| noclobber override and the <> read-write redirection operators, checked
# against dash. A real pipe after a file redirection stays a pipe, so the
# operators do not capture an unrelated bar or greater.

f=/tmp/shit_redirops_$$
rm -f "$f"

# >| writes even when noclobber would refuse a plain >.
set -C
echo first > "$f"
echo second >| "$f"
cat "$f"
set +C

# <> opens for reading and writing without truncating, so a short write leaves
# the rest of the file intact.
printf 'ABCDEF' > "$f"
exec 3<> "$f"
printf 'xy' >&3
exec 3>&-
cat "$f"
echo

# A bar that does not touch the greater is still a pipe stage.
echo piped > "$f" | cat
cat "$f"

rm -f "$f"
