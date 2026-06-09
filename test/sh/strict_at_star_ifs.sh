#!/bin/sh
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
