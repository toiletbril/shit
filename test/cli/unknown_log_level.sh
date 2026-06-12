unset SHIT_FLAGS
"$BIN" -X bogus -c true
echo "rc=$?"
"$BIN" -X debug --debug-logging-file=/dev/null -c true
echo "rc=$?"
