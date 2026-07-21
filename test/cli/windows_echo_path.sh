if [ "${OS-}" = Windows_NT ]; then
    path_value='C:\clear\e[2J\tail'
    output=$(PATH="$path_value" "$BIN" -c 'echo "$PATH"; echo survived')
    expected=$(printf '%s\n%s' "$path_value" survived)
    [ "$output" = "$expected" ] || exit 1

    [ "$("$BIN" -c 'printf "%s" C:\new')" = 'C:new' ] || exit 1
    [ "$("$BIN" -c "printf '%s' 'C:\new'")" = 'C:\new' ] || exit 1
    [ "$("$BIN" -c 'printf "%s" C:\\new')" = 'C:\new' ] || exit 1
    case "$("$BIN" --debug-highlight-at 'echo C:\Windows')" in
        *'C:\Windows'*) exit 1 ;;
    esac
fi

[ "$("$BIN" -c 'echo -e "a\tb"')" = "$(printf 'a\tb')" ] || exit 1
echo "Windows PATH echo stays literal"
echo "Windows paths retain the portable shell escape grammar"
