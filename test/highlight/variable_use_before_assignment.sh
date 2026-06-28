echo "== use before a standalone assignment, then after:"
"$BIN" --debug-highlight-at 'echo $V; V=1; echo $V'
echo "== a prefix assignment leaves the name unset for the same command:"
"$BIN" --debug-highlight-at 'V2=1 echo $V2'
echo "== a standalone assignment sets the name for what follows:"
"$BIN" --debug-highlight-at 'V3=1; echo $V3'
echo "== several prefix names all stay unset:"
"$BIN" --debug-highlight-at 'A=1 B=2 echo $A$B'
echo "== an array element assignment binds the base name:"
"$BIN" --debug-highlight-at 'echo $arr; arr[0]=1; echo $arr'
echo "== an exported name is set everywhere:"
"$BIN" --debug-highlight-at 'echo $PATH'
