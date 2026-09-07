unset KOSH_FLAGS
echo "== a select menu variable must be a plain name =="
"$BIN" --mood bash -c 'select $menu in a b c; do break; done' 2>&1
echo "rc=$?"
echo "== a quoted select menu variable is rejected the same way =="
"$BIN" --mood bash -c 'select "menu" in a b c; do break; done' 2>&1
echo "rc=$?"
echo "== a select menu variable that is not an identifier is rejected =="
"$BIN" --mood bash -c 'select 9bad in a b c; do break; done' 2>&1
echo "rc=$?"
echo "== a plain select menu variable is accepted =="
printf '1\n' | "$BIN" --mood bash -c 'select menu in a b; do echo "picked $menu"; break; done' 2>&1
echo "rc=$?"
echo "== select is not a keyword outside bash mood =="
"$BIN" -c 'select $menu in a b c; do break; done' 2>&1
echo "rc=$?"
echo "== a select menu word is checked against the sh shebang =="
"$BIN" --mood bash -n -WWW -c '#!/bin/sh
select menu in item=$"select locale"; do break; done' 2>&1
echo "rc=$?"
