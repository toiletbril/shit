unset SHIT_FLAGS
# type reports how a name resolves in the shell's own order, a keyword, an alias,
# a function, then a builtin. The -t form prints the class word, the default form
# spells it out, -a lists every location, and -p and -t stay silent for a name
# that is not a file. The names are kept off the PATH so the output is stable.
echo "== -t keyword:"; "$BIN" -c 'type -t for'
echo "== -t builtin:"; "$BIN" -c 'type -t echo'
echo "== -t alias:"; "$BIN" -c 'alias g=git; type -t g'
echo "== -t function:"; "$BIN" -c 'f(){ :; }; type -t f'
echo "== -t conditional bracket keyword:"; "$BIN" -c "type -t '[['"
echo "== default keyword spelling:"; "$BIN" -c 'type for'
echo "== default builtin spelling:"; "$BIN" -c 'type echo'
echo "== default alias spelling:"; "$BIN" -c 'alias g=git; type g'
echo "== default function spelling:"; "$BIN" -c 'f(){ :; }; type f'
echo "== -a lists a keyword with no file:"; "$BIN" -c 'type -a for'
echo "== -p stays silent for a builtin:"; "$BIN" -c 'type -p echo'; echo "rc=$?"
echo "== -t stays silent for a missing name:"; "$BIN" -c 'type -t missing_xyz'; echo "rc=$?"
echo "== a missing name reports not found:"; "$BIN" -c 'type missing_xyz'; echo "rc=$?"
