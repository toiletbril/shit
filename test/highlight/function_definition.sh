set -e

result=$("$BIN" --debug-highlight-at 'g; g() { :; }; g')
tab=$(printf '\t')
printf '%s\n' "$result" |
    grep -E "^g${tab}(unknown-command|function-name|resolved-command)$"
