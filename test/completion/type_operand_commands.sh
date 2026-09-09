echo "== alias and function operands:"
"$BIN" -c 'alias zzalias=echo; zzfunc() { :; }' \
    --debug-complete-at 'type zz' </dev/null
echo "== builtin operand:"
"$BIN" -c ':' --debug-complete-at 'type unali' </dev/null
echo "== keyword operand:"
"$BIN" -c ':' --debug-complete-at 'type whil' </dev/null
echo "== absent operand:"
"$BIN" -c ':' --debug-complete-at 'type zzz-absent' </dev/null
echo "== flags still answer:"
"$BIN" -c ':' --debug-complete-at 'type -' </dev/null
