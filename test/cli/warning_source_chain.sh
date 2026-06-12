unset SHIT_FLAGS
# A warning from a sourced file prints the chain that reached it, the same
# trace frames an error shows, while a warning in the typed line stays a
# single report.
outer=$(mktemp)
inner=$(mktemp)
cat > "$outer" <<EOF
outer=1
. $inner
EOF
cat > "$inner" <<'EOF'
inner=1
[[ -z "$UNSET_CHAIN" ]]
EOF
"$BIN" -W -c ". $outer" 2>&1 | sed "s|$outer|OUTER|; s|$inner|INNER|"
"$BIN" -W -c '[[ -z "$UNSET_FLAT" ]]' 2>&1 | grep -c trace
rm -f "$outer" "$inner"
echo "rc=$?"
