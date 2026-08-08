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


# The "$@" and "$*" positional expansions under a custom IFS, checked against
# dash. The star joins with the first IFS character while the at keeps each
# parameter as a separate word, and a quoted at survives an empty IFS.

set -- alpha beta gamma

# A custom IFS joins the star with its first character.
IFS=,
echo "star=[$*]"
echo "at=[$@]"

# A multi-character IFS still joins the star with only the first character.
IFS=-+
echo "star_multi=[$*]"

# An empty IFS concatenates the star with no separator.
IFS=
echo "star_empty=[$*]"

# A quoted at keeps each parameter as its own word regardless of IFS.
unset IFS
count=0
for one in "$@"; do
    count=$((count + 1))
    echo "word$count=$one"
done

# An unquoted star splits on the default whitespace.
set -- "x y" z
echo "unquoted_count_check"
n=0
for piece in $*; do
    n=$((n + 1))
done
echo "pieces=$n"

# In POSIX mode the dollar before a double quote stays a literal dollar, the
# bash locale-string meaning is suppressed, matching dash.
echo $"x"
v=val
echo $"a $v b"

# The re-splitting of an unquoted expansion under a custom IFS, checked against
# dash. A non-whitespace IFS keeps an empty field between two adjacent
# delimiters, while the default whitespace IFS folds runs and trims the ends.

# A colon IFS keeps the empty field that two adjacent colons produce.
record="a:b::d"
IFS=:
set -- $record
echo "fields=$#"
i=0
for field in $record; do
    i=$((i + 1))
    echo "field$i=[$field]"
done

# A trailing delimiter does not add a final empty field for a non-whitespace IFS.
trailing="x:y:"
set -- $trailing
echo "trailing_count=$#"

# The default whitespace IFS folds adjacent spaces and tabs into one split.
unset IFS
spaced="  one   two	three  "
set -- $spaced
echo "spaced_count=$#"
echo "first=[$1] last=[$3]"

# A quoted expansion is not split even under a custom IFS.
IFS=:
quoted="p:q:r"
set -- "$quoted"
echo "quoted_count=$#"
echo "quoted_one=[$1]"

# A prefix IFS=... assignment drives the word splitting of the command it
# precedes, including the read builtin, and does not persist after it, checked
# against dash. This is the splitting config.sub relies on.

IFS=- read a b c d <<HD
w-x-y-z
HD
echo "[$a][$b][$c][$d]"

# A second prefixed read splits on a different separator, and the last name
# receives the remainder of the line.
IFS=: read p q <<HD
left:right:extra
HD
echo "[$p][$q]"

# IFS is back to its whitespace default after the prefixed commands, so a later
# read splits on spaces rather than the prior separators.
printf 'one two three\n' | { read first rest; echo "[$first][$rest]"; }
