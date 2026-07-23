unset SHIT_FLAGS
"$BIN" --help 2>&1 | grep 'OPTIONS$'
"$BIN" --help 2>&1 | grep -c -- '--no-traces'
echo "rc=$?"
