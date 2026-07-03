unset SHIT_FLAGS
# A prefix IFS=... on read must split on that IFS even when a prior assignment
# left a different IFS in the store. sdkman reads its comma-separated candidate
# list this way after setting IFS to a newline, so a stale IFS broke its PATH
# setup.
echo "== a prefix IFS splits read after the global IFS was changed:"
"$BIN" --mood bash -s <<'EOF'
IFS=$'\n'
IFS=',' read -a parts <<< "a,b,c,d"
echo "count=${#parts[@]} first=${parts[0]} last=${parts[3]}"
EOF
echo "== a prefix IFS splits read with the default global IFS:"
"$BIN" --mood bash -s <<'EOF'
IFS=',' read -a parts <<< "x,y,z"
echo "${parts[0]}|${parts[1]}|${parts[2]}"
EOF
echo "== read without a prefix follows the changed global IFS:"
"$BIN" --mood bash -s <<'EOF'
IFS=,
read -a parts <<< "one,two,three"
echo "count=${#parts[@]}"
EOF
