set -e

tab=$(printf '\t')
log=$(mktemp)
trap 'rm -f "$log"' EXIT

"$BIN" -X all --debug-logging-file "$log" \
    -c 'alpha=1; beta=2; gamma=3' \
    --debug-highlight-at 'echo $($($($($(' </dev/null >/dev/null

"$BIN" -X all --debug-logging-file "$log" \
    --debug-highlight-at 'echo definitely-not-an-existing-path' \
    </dev/null >/dev/null

grep -E '(highlighting visited 0 variable names|the diagnostic highlight cache is stable 1|highlighting read 0 directories)$' "$log" |
    sed 's/^.*(): //'

"$BIN" --debug-highlight-at 'echo $(true # comment
inner-command) outer' </dev/null |
    grep -E "^(# comment${tab}comment|inner-command${tab}unknown-command)$"

"$BIN" \
    --debug-highlight-at 'echo $(( 1 + $(printf ")") + 2 )); echo after' \
    </dev/null |
    grep -E "${tab}(resolved-command|string)$"

"$BIN" -c 'probecmd() { :; }' \
    --debug-highlight-at 'echo $(case x in x) probecmd a;; esac)' </dev/null |
    grep -E "^(esac${tab}keyword|probecmd${tab}resolved-command)$"
