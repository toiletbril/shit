#!/bin/bash
# A backslash inside a double-quoted string within $(...) escapes the next
# char, so a \" does not close the string the way a bare " would. The closing
# ) of the substitution must land outside the quoted span, not inside it.
platform=linux
checksum=$(echo ".platforms[\"$platform\"].checksum // empty")
echo "[$checksum]"

# A single-quoted span inside $(...) keeps every char literal, so a \" there
# stays two characters and the next " still closes nothing.
literal=$(echo '.literal\"'$platform'\".')
echo "[$literal]"

# Adjacent escape and substitution across the double-quote span.
v=mid
mixed=$(echo "a\\b\"$v\"c\\d")
echo "[$mixed]"
