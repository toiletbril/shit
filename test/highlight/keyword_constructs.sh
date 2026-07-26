echo "== if then elif else fi:"
"$BIN" --debug-highlight-at 'if true; then echo a; elif false; then echo b; else echo c; fi'
echo "== while do done:"
"$BIN" --debug-highlight-at 'while true; do echo x; done'
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
"$BIN" --debug-highlight-at 'then; done; fi; esac; do echo a; in foo'
echo "== palette and lexical flags:"
"$BIN" --debug-highlight-at 'echo "green" - -x -- --long=value --color="$PATH" *.shit ./shit-highlight-missing-path-xyz; shit-highlight-missing-command-xyz'
