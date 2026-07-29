OLD_IFS=$IFS
IFS=$'\n'
scripts=($(printf 'first\nsecond\n'))
printf 'array=%s,%s\n' "${scripts[0]}" "${scripts[1]}"
IFS=$OLD_IFS
unset OLD_IFS scripts

if false; then
    printf 'unreachable inner branch\n'
fi
