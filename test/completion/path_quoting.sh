# A filesystem completion whose match carries a special byte is wrapped in
# single quotes rather than backslash-escaped, while a plain name stays bare. A
# hermetic temp directory keeps the candidates stable across machines. The typed
# prefix is one token, the special byte lives in the matched entry.
case "$BIN" in /*) ;; *) BIN=$(pwd)/$BIN ;; esac
dir=$(mktemp -d)
trap '[ -n "$dir" ] && /bin/rm -rf "$dir"' EXIT
: > "$dir/spacey file.txt"
: > "$dir/dollar\$x.txt"
: > "$dir/plainfile.txt"
/bin/mkdir "$dir/PATH" "$dir/space dir" "$dir/home" "$dir/\$HOME" "$dir/~"
/bin/mkdir "$dir/[C] alpha" "$dir/star* alpha" "$dir/question? alpha" \
    "$dir/outer" "$dir/outer/[C] alpha" "$dir/~root" "$dir/ME"
: > "$dir/single'quote.txt"
: > "$dir/double\$\"tick\`\\name.txt"
: > "$dir/foo_bar_baz"
: > "$dir/~root/literal-tilde.txt"
: > "$dir/ME/literal-variable.txt"
: > "$dir/PATH/plain.txt"
: > "$dir/PATH/space file.txt"
: > "$dir/PATH/star-one.txt"
: > "$dir/space dir/mixed.txt"
: > "$dir/home/home.txt"
: > "$dir/home/space file.txt"
: > "$dir/\$HOME/literal-variable.txt"
: > "$dir/~/literal-tilde.txt"
: > "$dir/PATH/tool"
chmod +x "$dir/PATH/tool"
cd "$dir"
HOME="$dir/home"
export HOME
echo "== a space in the match quotes it:"
"$BIN" --debug-complete-at 'cat spacey' </dev/null
echo "== a plain match stays unquoted:"
"$BIN" --debug-complete-at 'cat plain' </dev/null
echo "== a dollar in the match quotes it:"
"$BIN" --debug-complete-at 'cat dollar' </dev/null
echo "== inside a single quote the space match completes bare within it:"
"$BIN" --debug-complete-at "cat 'spacey" </dev/null
echo "== inside a double quote the space match completes bare within it:"
"$BIN" --debug-complete-at 'cat "spacey' </dev/null
echo "== glob-like bytes stay literal inside open quotes:"
for quote in "'" '"'; do
    for prefix in '[C]' 'star*' 'question?'; do
        "$BIN" --debug-complete-at "cd $quote$prefix" </dev/null
    done
done
echo "== an open quote after a directory keeps the full path prefix:"
"$BIN" --debug-complete-at 'cd outer/"[C]' </dev/null
echo "== an open quote inside a command keeps the prefix:"
PATH="$dir/PATH" "$BIN" --debug-complete-at 'e"c' </dev/null
echo "== a closed quote inside a command keeps the prefix:"
PATH="$dir/PATH" "$BIN" --debug-complete-at 'e"c"' </dev/null
echo "== a fuzzy prefix before an open quote maps into the candidate:"
"$BIN" --debug-complete-at 'cat f_b"z' </dev/null
echo "== open quotes escape bytes that remain active in their quote mode:"
"$BIN" --debug-complete-at "cat 'single" </dev/null
"$BIN" --debug-complete-at 'cat "double' </dev/null
echo "== an adjacent open double quote keeps variable expansion active:"
QUOTED_COMPLETION_UNIQUE=1 \
    "$BIN" --debug-complete-at 'cat prefix"$QUOTED_COMPLETION_U' </dev/null
echo "== a closed quoted directory continues through an unquoted slash:"
"$BIN" --debug-complete-at "cat 'PATH'/" </dev/null
echo "== a partial basename follows a closed quoted directory:"
"$BIN" --debug-complete-at "cat 'PATH'/pl" </dev/null
echo "== a quoted directory that contains a space keeps its spelling:"
"$BIN" --debug-complete-at "cat 'space dir'/mi" </dev/null
echo "== adjacent quoted and unquoted directory bytes are decoded together:"
"$BIN" --debug-complete-at "cat P'A'TH/pl" </dev/null
echo "== an escaped directory keeps its spelling:"
"$BIN" --debug-complete-at 'cat space\ dir/mi' </dev/null
echo "== a double-quoted variable directory remains active:"
"$BIN" --debug-complete-at 'cat "$HOME"/' </dev/null
echo "== a single-quoted variable directory remains literal:"
"$BIN" --debug-complete-at "cat '\$HOME'/" </dev/null
echo "== an open single-quoted variable directory remains literal:"
"$BIN" --debug-complete-at "cat '\$HOME/" </dev/null
echo "== an open double-quoted variable directory remains active:"
"$BIN" --debug-complete-at 'cat "$HOME/' </dev/null
echo "== an active tilde directory expands:"
"$BIN" --debug-complete-at 'cat ~/' </dev/null
echo "== an active tilde stays outside a quoted match:"
"$BIN" --debug-complete-at 'cat ~/sp' </dev/null
echo "== an active variable uses escapes for a matched space:"
"$BIN" --debug-complete-at 'cat $HOME/sp' </dev/null
echo "== a quoted tilde directory remains literal:"
"$BIN" --debug-complete-at "cat '~'/" </dev/null
echo "== quoted and escaped tilde usernames remain literal:"
"$BIN" --debug-complete-at 'cat ~"root"/lit' </dev/null
"$BIN" --debug-complete-at 'cat ~r\oot/lit' </dev/null
echo "== quoted and escaped bytes stop an unbraced variable name:"
HO= "$BIN" --debug-complete-at 'cat $HO"ME"/lit' </dev/null
HO= "$BIN" --debug-complete-at 'cat $HO\ME/lit' </dev/null
echo "== a quoted glob remains literal:"
"$BIN" --debug-complete-at "cat 'PATH'/'*.txt'" </dev/null
echo "== an unquoted glob after a quoted directory expands:"
"$BIN" --debug-complete-at "cat 'PATH'/*.txt" </dev/null
echo "== command position keeps only runnable files:"
"$BIN" --debug-complete-at "'PATH'/" </dev/null
