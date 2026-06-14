unset SHIT_FLAGS
# echo --help stays literal text in the posix and bash moods and with extra
# arguments, since scripts print the word.
"$BIN" --mood sh -c 'echo --help'
"$BIN" --mood bash -c 'echo --help'
"$BIN" -c 'echo --help foo'
"$BIN" --mood bash -c 'test --help && echo nonempty'
"$BIN" -c '[ --help ] && echo expr'
echo "rc=$?"
