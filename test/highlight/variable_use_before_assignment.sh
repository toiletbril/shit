set -e

result=$("$BIN" --debug-highlight-at \
    'echo $V; V=1; echo $V; V2=1 echo $V2; echo $arr; arr[0]=1; echo $arr; echo $PATH')
tab=$(printf '\t')
printf '%s\n' "$result" |
    grep -E "${tab}(unset-variable|variable|assignment-name)$"
