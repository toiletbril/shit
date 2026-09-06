echo "== set --tab-selector value:"
"$BIN" --debug-complete-at 'set --tab-selector ' </dev/null
echo "== set --tab-selector= value:"
"$BIN" --debug-complete-at 'set --tab-selector=' </dev/null
echo "== set --tab-selector= prefix:"
"$BIN" --debug-complete-at 'set --tab-selector=e' </dev/null
echo "== kosh --tab-selector value:"
"$BIN" --debug-complete-at 'kosh --tab-selector ' </dev/null
echo "== kosh --tab-selector= prefix:"
"$BIN" --debug-complete-at 'kosh --tab-selector=p' </dev/null
echo "== the flag name itself:"
"$BIN" --debug-complete-at 'kosh --tab-s' </dev/null
echo "== set flag name:"
"$BIN" --debug-complete-at 'set --tab-s' </dev/null
echo "== an unrelated command keeps its own candidates:"
"$BIN" --debug-complete-at 'echo --tab-selector ' </dev/null | grep -c '^interactive$'
