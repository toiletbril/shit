echo "== read -q returns zero for y:"
printf 'y\n' | "$BIN" -c 'read -r -q; echo "rc=$?"'
echo "== read -q returns zero for Y:"
printf 'Y\n' | "$BIN" -c 'read -r -q; echo "rc=$?"'
echo "== read -q returns one for n:"
printf 'n\n' | "$BIN" -c 'read -r -q; echo "rc=$?"'
echo "== read -q returns one for a non-y byte:"
printf 'x\n' | "$BIN" -c 'read -r -q; echo "rc=$?"'
echo "== read -q at EOF returns one:"
"$BIN" -c 'read -r -q </dev/null; echo "rc=$?"'
