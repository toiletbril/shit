unset SHIT_FLAGS
"$BIN" --help 2>&1 | grep 'OPTIONS$'
echo "rc=$?"
