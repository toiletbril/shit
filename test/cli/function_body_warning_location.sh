unset SHIT_FLAGS
# A warning from a function body called after its defining source is gone
# renders against the function's stored definition copy, with the defining
# file's name, its absolute line numbers, and the caret on the reference.
# A fixed path rather than mktemp, so the warning column, which counts into the
# real source path, is the same on every platform and the golden stays
# portable. mktemp yields a longer path on macOS than on Linux.
lib=/tmp/shit_fbwl_lib
cat > "$lib" <<'EOF'
lib_marker=1
probe_fn() {
  echo "first=${UNSET_FN_PROBE}"
  echo $((UNSET_FN_ARITH + 1))
  echo "line=$LINENO"
}
EOF
"$BIN" -W -c ". $lib; probe_fn" 2>&1 | sed "s|$lib|LIB|" | ./normalize-trace.sh "$BIN"
rm -f "$lib"
echo "rc=$?"
