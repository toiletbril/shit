#!/bin/sh
# The test builtin's file comparison operators -ef, -nt, and -ot, checked
# against dash. The timestamps are set explicitly so the ordering is stable.

base=/tmp/shit_filecmp_$$
older="$base.old"
newer="$base.new"
rm -f "$older" "$newer" "$base.link"
: > "$older"
: > "$newer"
touch -t 202001010000 "$older"
touch -t 203001010000 "$newer"

test "$newer" -nt "$older" && echo "newer_nt_older"
test "$older" -ot "$newer" && echo "older_ot_newer"
test "$older" -nt "$newer" || echo "older_not_nt_newer"

# A file is the same file as itself, and a hard link names the same file.
test "$older" -ef "$older" && echo "self_ef"
ln "$older" "$base.link"
test "$older" -ef "$base.link" && echo "hardlink_ef"
test "$older" -ef "$newer" || echo "distinct_not_ef"

# A comparison against a missing file is false.
rm -f "$base.gone"
test "$older" -ef "$base.gone" || echo "missing_not_ef"

rm -f "$older" "$newer" "$base.link"
