echo "== a bare definition colors the name and a later call resolves:"
"$BIN" --debug-highlight-at 'f() { :; }; f'
echo "== a call before the definition stays unknown:"
"$BIN" --debug-highlight-at 'g; g() { :; }; g'
echo "== the function keyword form colors the name:"
"$BIN" --debug-highlight-at 'function h { :; }; h'
