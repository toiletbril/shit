#!/bin/sh
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
