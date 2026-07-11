unset SHIT_FLAGS
# caller returns 1 at the top level and prints the frame line and source inside
# a function. A non-numeric operand is a located error pointing at the operand.
# Fixed paths rather than mktemp, so the caret width, which spans the real
# source path, is the same length on every platform and the golden stays
# portable.
src=/tmp/shit_caller_src
cat > "$src" <<'EOF'
f() {
  caller 0
}
f
EOF

echo "== caller at top level returns 1:"
"$BIN" -c 'caller'; echo "rc=$?"
echo "== caller 0 inside a function prints line and source:"
"$BIN" -c ". $src" 2>&1 | sed "s|$src|SRC|"
echo "== caller with a non-numeric operand is a located error:"
"$BIN" -c 'caller abc' 2>&1; echo "rc=$?"
rm -f "$src"
