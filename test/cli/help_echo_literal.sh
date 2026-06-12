unset SHIT_FLAGS
# echo --help stays literal text in the posix and bash moods and with extra
# arguments, since scripts print the word.
"$BIN" -P -c 'echo --help'
"$BIN" --bash-compatible -c 'echo --help'
"$BIN" -c 'echo --help foo'
"$BIN" --bash-compatible -c 'test --help && echo nonempty'
"$BIN" -c '[ --help ] && echo expr'
echo "rc=$?"
