#!/bin/bash
# Bash $'...' edge escapes the existing ansi_c_quoting test does not cover.
# Four hex byte forms, the long and short unicode escapes, octal with and
# without a leading zero, \e, and a hex escape that stops at two digits before
# trailing text. Bytes go through od so a control char shows as a stable token.
echo $'\x9\x0a\x7e' | od -An -c | tr -s ' '
echo $'\u41\u42\u43'
echo $'é'
echo $'\U0001F600' | od -An -c | tr -s ' '
echo $'\0101\0102' | od -An -c | tr -s ' '
echo $'\7\10\11' | od -An -c | tr -s ' '
echo $'a\eb' | od -An -c | tr -s ' '
echo $'\x41bcd'
echo $'mix\tof\\stuff\x21'


# Bash line continuation. A backslash at end of line is removed with its
# newline outside quotes and inside double quotes, so the two halves join. A
# backslash newline inside single quotes is kept literal, and inside $'...' a
# backslash newline is removed.
echo one\
two
echo "three\
four"
echo 'five\
six' | od -An -c | tr -s ' '
echo $'seven\
eight'
abc\
def=ok
echo "$abcdef"
echo before \
after

# Bash $'...' numeric escape edges. Octal takes up to three digits so a fourth
# digit is a literal that follows, the max octal byte is \377, a hex escape
# takes up to two digits and stops at a non-hex char, and an unknown backslash
# escape keeps both the backslash and the char.
echo $'\377' | od -An -c | tr -s ' '
echo $'\1011' | od -An -c | tr -s ' '
echo $'\x41\x42' | od -An -c | tr -s ' '
echo $'\x4g' | od -An -c | tr -s ' '
echo $'\\' | od -An -c | tr -s ' '
echo $'no\qescape' | od -An -c | tr -s ' '
echo $'\060\061\062'
echo $'\x2f\x2e'

# Bash splits an unquoted expansion on IFS into separate words and drops empty
# fields from whitespace runs, while a double-quoted expansion stays one word
# with its spaces intact. A custom IFS changes the split character, and the
# quoted form ignores it.
v="one two   three"
printf '[%s]\n' $v
printf '[%s]\n' "$v"
IFS=:
p="a:b::c"
printf '[%s]\n' $p
printf '[%s]\n' "$p"
IFS=$' \t\n'
echo "---"
w="  lead trail  "
printf '[%s]\n' $w
printf '[%s]\n' "$w"

# Bash single quotes keep every byte literal, no backslash, dollar, or backtick
# is special, and a single quote cannot appear inside. A lone backslash and an
# embedded newline are kept as written.
echo 'no $expand `cmd` \t \n \\ "dq" literal'
v=World
echo 'single $v stays'
echo '\'
printf '%s\n' 'a
b'
echo 'tab	and spaces   kept' | od -An -c | tr -s ' '

# Bash treats a glob metacharacter as literal when it is quoted or backslash
# escaped, so the word stays as written instead of matching a path. An unquoted
# pattern that matches nothing stays literal too under the default nullglob off,
# and these patterns are chosen to match nothing in any directory.
echo '*'
echo "?"
echo \[abc\]
echo '*.nomatchxyz'
echo "a*b?c[d]"
echo /no_such_dir_zzz/*
echo \*\?\[
printf '[%s]\n' '*'

# Bash joins adjacent quoted and unquoted parts of one word with no separator.
# A single-quoted part, a double-quoted part, an unquoted part, and a $'...'
# part all fuse into a single argument, and expansion happens only in the parts
# that allow it.
v=mid
echo 'a'"b"c$'\x64'
echo pre"$v"post
echo "$v"'lit'$v
echo a''b""c
echo \"quoted\"and'more'
echo $'x\ty'"$v"'z'
printf '[%s]\n' "one"'two'three

# Bash double-quote escaping. A backslash inside double quotes is special only
# before $, `, ", \, and newline. Before any other char the backslash stays
# literal. The dollar, backtick, and quote forms escape their char, and a
# doubled backslash collapses to one.
echo "a\$b"
echo "a\`b"
echo "a\"b"
echo "a\\b"
echo "a\cb"
echo "a\tb"
echo "back\\slash\\\\end"
v=value
echo "lit \$v stays, real $v expands"
echo "path\\to\\thing"
echo "trailing backslash\\"

printf '%s' $'\cA\cB\cC\cI\cJ' | od -An -tx1 | tr -s ' '
printf '%s' $'\c[\c\\\c]' | od -An -tx1 | tr -s ' '
printf '%s' $'\cm\cz\cZ' | od -An -tx1 | tr -s ' '
printf 'after=%s\n' $'\cGdone'
printf '%s' $'\c1\c0\c9' | od -An -tx1 | tr -s ' '
printf '%s' $'\c!\c~\c}\c@' | od -An -tx1 | tr -s ' '
printf '%s' $'\c?' | od -An -tx1 | tr -s ' '

# Bash $'...' ANSI-C quoting, checked byte-for-byte against bash. Covers the
# named escapes, hex and octal bytes, a unicode escape, quote and backslash
# escapes, and concatenation with surrounding text.
echo $'hello\nworld'
echo $'tab\there'
echo $'\x41\x42\x43'
echo $'\101\102\103'
echo $'quote\'s'
echo $'back\\slash'
echo $'bell\aend' | od -An -c | tr -s ' '
echo $'é'
v=$'a\tb\tc'
echo "$v"
echo pre$'\t'post
echo $''
echo $'\e[1m' | od -An -c | tr -s ' '
printf '%s\n' $'one\ntwo'

# Inside double quotes $'...' is not ANSI-C quoting, so the $' is literal and
# bash prints the three bytes. It decodes only in an unquoted or bare context.
echo "$'x'"
echo "a$'\t'b"
echo "$'\n'"
printf '%s\n' $'a\tb'

echo $"plain locale string"
x=world
echo $"hello $x"
echo pre$"mid"post
echo "$"
printf '[%s]\n' $"a b"

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
