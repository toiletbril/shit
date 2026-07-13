unset SHIT_FLAGS
"$BIN" -c 'local --help' 2>&1 | ./normalize-trace.sh "$BIN"
echo "rc=${PIPESTATUS[0]}"
