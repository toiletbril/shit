#!/bin/sh
# The colon-form parameter expansions and the prefix and suffix trims, checked
# against dash. The assign form writes back into the variable, the alternate
# forms distinguish unset from empty, and the percent and hash trims take the
# shortest and longest match.

# The assign form sets the variable when it is unset and yields the value.
unset target
echo "assign=${target:=written}"
echo "after_assign=$target"

# The alternate form distinguishes an empty value from an unset name.
empty=
echo "alt_empty=[${empty:+nonempty}]"
echo "alt_set=[${empty+set_even_if_empty}]"
echo "alt_unset=[${missing+would_be}]"

# The default form leaves the variable untouched.
unset def
echo "default=${def:-fallback}"
echo "still_unset=[${def-unset_marker}]"

# The suffix trims take the shortest and the longest match.
path=usr.local.bin.tool
echo "pct=${path%.*}"
echo "pctpct=${path%%.*}"

# The prefix trims take the shortest and the longest match.
echo "hash=${path#*.}"
echo "hashhash=${path##*.}"

# The length form counts characters.
word=length
echo "len=${#word}"
