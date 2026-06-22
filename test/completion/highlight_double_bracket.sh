unset SHIT_FLAGS
# The [[ conditional highlights as a keyword, distinct from the [ test builtin,
# and the closing ]] is a keyword too. In the sh mood, where [[ is not a reserved
# word, it falls through to the command coloring the way the parser rejects it.
# Each span prints as its text, a tab, and the color escape with the escape byte
# shown as \e. Green is \e[32m, bright blue is \e[94m, red is \e[31m.
echo "== [[ conditional, default mood =="
"$BIN" --debug-highlight-at '[[ -n $x ]]'
echo "== [ test builtin, default mood =="
"$BIN" --debug-highlight-at '[ -n "$x" ]'
echo "== [[ in sh mood is not a keyword =="
"$BIN" --mood sh --debug-highlight-at '[[ -n $x ]]'
