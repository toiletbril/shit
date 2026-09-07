set -e

tab=$(printf '\t')
quote=$(printf "'")

result=$("$BIN" --debug-highlight-at "v=\$${quote}a\\${quote}b${quote}; echo done")
printf '%s\n' "$result" | grep -Fx "\$${quote}a\\${quote}b${quote}${tab}string"
printf '%s\n' "$result" | grep -Fx "echo${tab}resolved-command"

result=$("$BIN" --debug-highlight-at "chars=\$${quote}\\t\\n !\"&\\${quote}();<>|${quote} # a note
echo after")
printf '%s\n' "$result" | grep -Fx "echo${tab}resolved-command"

result=$("$BIN" --debug-highlight-at "v=\$${quote}plain${quote}; echo tail")
printf '%s\n' "$result" | grep -Fx "\$${quote}plain${quote}${tab}string"
printf '%s\n' "$result" | grep -Fx "echo${tab}resolved-command"

result=$("$BIN" --debug-highlight-at 'v=$"plain $HOME $(echo nested)"')
printf '%s\n' "$result" | grep -Fx "\$\"plain ${tab}string"
printf '%s\n' "$result" | grep -Fx "\$HOME${tab}variable"
printf '%s\n' "$result" | grep -Fx "echo${tab}resolved-command"
printf '%s\n' "$result" | grep -Fx "\"${tab}string"
