# Inline glob completion (echo *<TAB>) backslash escapes each expanded filename.
# A name with a space, parens, a glob character, a backtick, a dollar, a double
# quote, a bang, or a single quote inserts as valid shell input.
d=$(mktemp -d)
cd "$d" || exit 1
: > 'a b.txt'
: > 'foo(1).sh'
: > 'star*.txt'
: > 'back`tick.txt'
: > 'dollar$x.txt'
: > 'double".txt'
: > 'bang!.txt'
: > normal.txt
: > "sq'back\`.txt"
: > "sq'dollar\$.txt"
: > "sq'double\".txt"
: > "sq'bang!.txt"
echo "== inline glob escaping:"
"$BIN" --debug-complete-at 'echo *' </dev/null 2>/dev/null
cd / || exit 1
rm -rf "$d"
