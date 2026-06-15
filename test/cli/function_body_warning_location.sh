unset SHIT_FLAGS
# A warning from a function body called after its defining source is gone
# renders against the function's stored definition copy, with the defining
# file's name, its absolute line numbers, and the caret on the reference. The
# library is created with a fixed short name in a scratch directory and sourced
# by that name, so the rendered columns stay identical on every platform rather
# than tracking the length of a mktemp path.
case "$BIN" in /*) ;; *) BIN="$PWD/$BIN" ;; esac
dir=$(mktemp -d)
cat > "$dir/LIB" <<'EOF'
lib_marker=1
probe_fn() {
  echo "first=${UNSET_FN_PROBE}"
  echo $((UNSET_FN_ARITH + 1))
  echo "line=$LINENO"
}
EOF
( cd "$dir" && "$BIN" -W -c ". LIB; probe_fn" ) 2>&1
rm -rf "$dir"
echo "rc=$?"
