unset SHIT_FLAGS
# A warning from a sourced file prints the chain that reached it, the same
# trace frames an error shows, while a warning in the typed line stays a
# single report. The sourced files are created with fixed short names in a
# scratch directory and sourced by those names, so the trace carets stay
# identical on every platform rather than tracking the length of a mktemp path.
case "$BIN" in /*) ;; *) BIN="$PWD/$BIN" ;; esac
dir=$(mktemp -d)
cat > "$dir/OUTER" <<'EOF'
outer=1
. INNER
EOF
cat > "$dir/INNER" <<'EOF'
inner=1
[[ -z "$UNSET_CHAIN" ]]
EOF
( cd "$dir" && "$BIN" -W -c ". OUTER" ) 2>&1
"$BIN" -W -c '[[ -z "$UNSET_FLAT" ]]' 2>&1 | grep -c trace
rm -rf "$dir"
echo "rc=$?"
