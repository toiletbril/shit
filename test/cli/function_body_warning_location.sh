# shellcheck disable=SC2154
unset KOSH_FLAGS
# A warning from a function body called after its defining source is gone
# renders against the function's stored definition copy, with the defining
# file's name, its absolute line numbers, and the caret on the reference.
lib=$TEST_TEMP_DIRECTORY/function-body-warning
"$BIN" -c 'koshkit mkdir -p "$1"' setup "$TEST_TEMP_DIRECTORY"
cat > "$lib" <<'EOF'
lib_marker=1
probe_fn() {
  echo "first=${UNSET_FN_PROBE}"
  echo $((UNSET_FN_ARITH + 1))
  echo "line=$LINENO"
}
EOF
"$BIN" -WWW -c ". $lib; probe_fn" 2>&1 | sed "s|$lib|LIB|" | ./normalize-trace.sh "$BIN"
"$BIN" -c 'koshkit unlink "$1"' cleanup "$lib"
echo "rc=$?"

# A body that opens on the defining file's first line sits one line ahead of
# the header the definition copy carries, so its reported lines shift back.
first=$TEST_TEMP_DIRECTORY/function-body-warning-first
cat > "$first" <<'EOF'
first_fn() {
  echo "first=${UNSET_FIRST_PROBE}"
  echo "line=$LINENO"
}
EOF
"$BIN" -WWW -c ". $first; first_fn" 2>&1 | sed "s|$first|FIRST|" | ./normalize-trace.sh "$BIN"
"$BIN" -c 'koshkit unlink "$1"' cleanup "$first"
echo "rc=$?"
