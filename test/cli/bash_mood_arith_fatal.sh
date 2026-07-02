unset SHIT_FLAGS
echo "== bash mood aborts on an arithmetic error, the way a non-interactive bash does:"
"$BIN" --mood bash -c 'echo $((1/0)); echo after'
echo "rc=$?"
echo "== the strict default mood also aborts:"
"$BIN" -c 'echo $((1/0)); echo after'
echo "rc=$?"
echo "== bash mood still continues past an ordinary command failure:"
"$BIN" --mood bash -c 'false; echo after'
echo "rc=$?"
echo "== bash mood still continues past a readonly reassignment, matching bash:"
"$BIN" --mood bash -c 'readonly r=1; r=2; echo after'
echo "rc=$?"
