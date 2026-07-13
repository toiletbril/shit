unset SHIT_FLAGS
# local declares a function-scoped variable and rejects use outside a function.
# The attribute flags declare an integer, an indexed array, and an associative
# array, the same letters declare takes.
echo "== local outside a function is an error:"
"$BIN" -c 'local x=5' 2>&1 | ./normalize-trace.sh "$BIN"; rc=${PIPESTATUS[0]}; echo "rc=$rc"
echo "== local -i evaluates an integer assignment:"
"$BIN" -c 'f(){ local -i n=3+4; echo $n; }; f'
echo "== local -a declares an indexed array:"
"$BIN" -c 'f(){ local -a a; a[0]=x; a[1]=y; echo "${a[0]}${a[1]}"; }; f'
echo "== local -A declares an associative array:"
"$BIN" -c 'f(){ local -A m; m[key]=val; echo "${m[key]}"; }; f'
echo "== a local shadows an outer value and restores it:"
"$BIN" -c 'v=outer; f(){ local v=inner; echo $v; }; f; echo $v'
