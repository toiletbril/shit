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
echo "== the default mood classifies a missing coreutility fallback:"
PATH= "$BIN" -c 'type -t calc; command -v calc; shitbox which calc'
type_path=$(mktemp -d)
mkdir "$type_path/blocked" "$type_path/runnable"
printf '#!/bin/sh\n' > "$type_path/blocked/calc"
printf '#!/bin/sh\n' > "$type_path/runnable/calc"
chmod +x "$type_path/runnable/calc"
resolved_default=$(PATH="$type_path/blocked:$type_path/runnable" \
    "$BIN" -c 'type calc')
resolved_path=$(PATH="$type_path/blocked:$type_path/runnable" \
    "$BIN" -c 'type -p calc')
resolved_forced_path=$(PATH="$type_path/blocked:$type_path/runnable" \
    "$BIN" -c 'type -P calc')
echo "== type skips a blocked candidate before a runnable candidate:"
if test "$resolved_default" = "calc is $type_path/runnable/calc" &&
    test "$resolved_path" = "$type_path/runnable/calc" &&
    test "$resolved_forced_path" = "$type_path/runnable/calc"
then
    echo runnable
else
    echo blocked
fi
test -n "$type_path" && /bin/rm -rf "$type_path"
echo "== a missing name reports not found:"; "$BIN" -c 'type missing_xyz'; echo "rc=$?"
