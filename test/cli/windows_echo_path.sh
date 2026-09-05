if [ "${OS-}" = Windows_NT ]; then
    path_value='C:\clear\e[2J\tail'
    output=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$path_value" \
        "$BIN" -c 'echo "$PATH"; echo survived')
    expected=$(printf '%s\n%s' "$path_value" survived)
    [ "$output" = "$expected" ] || exit 1

    output=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$path_value" \
        "$BIN" -c '# shellcheck disable=SC2123
PATH="C:\updated"; koshkit env | koshkit grep "^Path="')
    [ "$output" = 'Path=C:\updated' ] || exit 1

    printf '@echo off\r\necho path-refresh-ran\r\n' > "$TEST_SYSTEM_PATH/path-refresh.bat"
    output=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$path_value" \
        "$BIN" -c 'Path="$TEST_SYSTEM_PATH"; path-refresh')
    [ "$output" = path-refresh-ran ] || exit 1

    [ "$("$BIN" --no-annoying-diagnostics -c 'printf "%s" C:\new')" = 'C:new' ] || exit 1
    [ "$("$BIN" -c "printf '%s' 'C:\new'")" = 'C:\new' ] || exit 1
    [ "$("$BIN" -c 'printf "%s" C:\\new')" = 'C:\new' ] || exit 1
    if "$BIN" --debug-highlight-at '' </dev/null >/dev/null 2>&1; then
        case "$("$BIN" --debug-highlight-at 'echo C:\Windows')" in
            *'C:\Windows'*) exit 1 ;;
        esac
    fi
fi

[ "$("$BIN" -c 'echo -e "a\tb"')" = "$(printf 'a\tb')" ] || exit 1
echo "Windows PATH echo stays literal"
echo "Windows paths retain the portable shell escape grammar"
