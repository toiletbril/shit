SHIT_TEST_HIGHLIGHT_STATS=1 "$BIN" \
    -c 'alpha=1; beta=2; gamma=3' \
    --debug-highlight-at 'echo $($($($($(' </dev/null

echo '== a substitution comment ends at its newline:'
line='echo $(true # comment
echo inner) outer'
"$BIN" --debug-highlight-at "$line" </dev/null

echo '== nested command substitution does not close arithmetic:'
"$BIN" --debug-highlight-at 'echo $(( 1 + $(printf ")") + 2 )); echo after' \
    </dev/null

echo '== a case pattern does not close command highlighting:'
"$BIN" -c 'probecmd() { :; }' \
    --debug-highlight-at 'echo $(case x in x) probecmd a;; esac)' </dev/null
