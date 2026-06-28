echo "== if then elif else fi:"
"$BIN" --debug-highlight-at 'if true; then echo a; elif false; then echo b; else echo c; fi'
echo "== while do done:"
"$BIN" --debug-highlight-at 'while true; do echo x; done'
echo "== until do done:"
"$BIN" --debug-highlight-at 'until false; do echo x; done'
echo "== for in do done:"
"$BIN" --debug-highlight-at 'for q in 1 2; do echo $q; done'
echo "== case esac:"
"$BIN" --debug-highlight-at 'case x in y) echo z;; esac'
echo "== bracket conditional:"
"$BIN" --debug-highlight-at '[[ -n x ]] && echo y'
echo "== function keyword:"
"$BIN" --debug-highlight-at 'function f { :; }; f'
echo "== time keyword:"
"$BIN" --debug-highlight-at 'time echo hi'
echo "== misplaced then done fi esac do as a command:"
"$BIN" --debug-highlight-at 'then echo a'
"$BIN" --debug-highlight-at 'done'
"$BIN" --debug-highlight-at 'fi'
"$BIN" --debug-highlight-at 'esac'
"$BIN" --debug-highlight-at 'do echo a'
echo "== misplaced in as a command:"
"$BIN" --debug-highlight-at 'in foo'
