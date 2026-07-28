set -e

result=$("$BIN" --debug-highlight-at 'echo x >/dev/null')
tab=$(printf '\t')
printf '%s\n' "$result" | grep -Fx "/dev/null${tab}existing-path"
