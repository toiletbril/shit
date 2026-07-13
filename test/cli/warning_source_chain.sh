unset SHIT_FLAGS
# A warning from a sourced file prints the chain that reached it, the same
# trace frames an error shows, while a warning in the typed line stays a
# single report.
# Fixed paths rather than mktemp, so the trace caret width, which spans the real
# source path, is the same length on every platform and the golden stays
# portable. mktemp yields a longer path on macOS than on Linux.
outer=/tmp/shit_wsc_outer
inner=/tmp/shit_wsc_inner
cat > "$outer" <<EOF
outer=1
. $inner
EOF
cat > "$inner" <<'EOF'
inner=1
[[ x = "$UNSET_CHAIN" ]]
EOF
"$BIN" -W -c ". $outer" 2>&1 | sed "s|$outer|OUTER|; s|$inner|INNER|" | ./normalize-trace.sh "$BIN"
"$BIN" -W -c '[[ x = "$UNSET_FLAT" ]]' 2>&1 | grep -c trace
rm -f "$outer" "$inner"
echo "rc=$?"
