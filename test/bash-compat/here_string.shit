#!/bin/bash
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
