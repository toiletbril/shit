unset SHIT_FLAGS
# calc keeps its unset-variable error and its lazy binding in every mood, not
# only the default. The bash and posix moods differ only in the output width,
# where the value wraps to 64 bits rather than the default 128.

echo "== bash mood: an unset variable is an error, not a silent zero:"
printf 'das\n' | "$BIN" -M bash -c 'calc -i'

echo "== bash mood: a name=value binding recomputes lazily on each read:"
printf 'y=2\nformula=x+y\nx=5\nformula\n' | "$BIN" -M bash -c 'calc -i'

echo "== sh mood: the same unset error applies:"
printf 'nope\n' | "$BIN" -M sh -c 'calc -i'

echo "== bash mood wraps the overflow to 64 bits:"
"$BIN" -M bash -c 'calc "9223372036854775807 + 1"'

echo "== the default mood keeps the full 128-bit value:"
"$BIN" -c 'calc "9223372036854775807 + 1"'
