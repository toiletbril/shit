# Completion matches in tiers: an exact prefix wins, then a smart-case prefix
# corrects the casing, then a subsequence such as fbb inside foo_bar_baz. Only
# the strongest tier is offered, so a fuzzy match never dilutes a real prefix.
# Smart case makes an all-lowercase token match either case while a token with an
# uppercase byte stays case sensitive. A single byte or an option dash is too
# loose for a subsequence and matches nothing extra.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
: > "$dir/README"
: > "$dir/Makefile"
: > "$dir/makefile.bak"
: > "$dir/foo_bar_baz"
: > "$dir/plain.txt"
export PATH="$dir"
cd "$dir"
echo "== an exact prefix hides the case-insensitive match:"
"$BIN" --debug-complete-at 'cat make' </dev/null
echo "== a lowercase token corrects to the uppercase name when nothing prefixes exactly:"
"$BIN" --debug-complete-at 'cat readme' </dev/null
echo "== an uppercase byte forces a case-sensitive match:"
"$BIN" --debug-complete-at 'cat READ' </dev/null
echo "== a subsequence matches a name-like token of two or more bytes:"
"$BIN" --debug-complete-at 'cat fbb' </dev/null
echo "== a single byte does not fuzzy match, only its exact prefixes:"
"$BIN" --debug-complete-at 'cat p' </dev/null
