unset KOSH_FLAGS
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
PATH= "$BIN" -c 'type -t calc; command -v calc; koshkit which calc'
type_path=$(mktemp -d)
mkdir "$type_path/blocked" "$type_path/runnable"
mkdir "$type_path/blocked/calc"
printf '#!/bin/sh\n' > "$type_path/runnable/calc"
chmod +x "$type_path/runnable/calc"
normalized_type_path=$(printf '%s\n' "$type_path" | tr '\\' '/')
resolved_default=$(env -u PATH \
    "$TEST_PATH_ENVIRONMENT_NAME=$type_path/blocked${TEST_PATH_SEPARATOR}$type_path/runnable" \
    "$BIN" -c 'type calc' | tr '\\' '/')
resolved_path=$(env -u PATH \
    "$TEST_PATH_ENVIRONMENT_NAME=$type_path/blocked${TEST_PATH_SEPARATOR}$type_path/runnable" \
    "$BIN" -c 'type -p calc' | tr '\\' '/')
resolved_forced_path=$(env -u PATH \
    "$TEST_PATH_ENVIRONMENT_NAME=$type_path/blocked${TEST_PATH_SEPARATOR}$type_path/runnable" \
    "$BIN" -c 'type -P calc' | tr '\\' '/')
echo "== type skips a blocked candidate before a runnable candidate:"
if test "$resolved_default" = "calc is $normalized_type_path/runnable/calc" &&
    test "$resolved_path" = "$normalized_type_path/runnable/calc" &&
    test "$resolved_forced_path" = "$normalized_type_path/runnable/calc"
then
    echo runnable
else
    echo blocked
fi
test -n "$type_path" && rm -rf "$type_path"
echo "== a missing name reports not found:"; "$BIN" -c 'type missing_xyz'; echo "rc=$?"
# The -V form adds everything the shell holds beyond the class word. A function
# carries the line its definition starts on and the body as the shell recorded
# it, a builtin and a bundled utility carry their description, and a keyword and
# an alias carry nothing more. A terse flag keeps its own shape and ignores -V.
echo "== -V prints a function body:"; "$BIN" -c 'f(){ :; }
type -V f'
echo "== -V reports the line a later definition starts on:"; "$BIN" -c 'x=1
g() {
  echo body
}
type -V g'
echo "== -V names the file a sourced function came from:"
type_source=$(mktemp -d)
printf '%s\n' 'sourced_helper() {' '  echo helper' '}' > "$type_source/helper.sh"
normalized_source=$(printf '%s\n' "$type_source" | tr '\\' '/')
sourced_head=$("$BIN" -c '. "$1/helper.sh"; type -V sourced_helper' kosh \
    "$type_source" | head -n 1 | tr '\\' '/')
if test "$sourced_head" = "sourced_helper is a shell function defined in $normalized_source/helper.sh on line 1"
then
    echo named
else
    echo "unexpected: $sourced_head"
fi
test -n "$type_source" && rm -rf "$type_source"
echo "== -V describes a builtin:"; "$BIN" -c 'type -V echo'
echo "== -V describes a bundled utility:"; PATH= "$BIN" -c 'type -V tsort'
echo "== -V adds nothing to a keyword or an alias:"
"$BIN" -c 'alias g=git; type -V for; type -V g'
echo "== a terse flag ignores -V:"
"$BIN" -c 'f(){ :; }; type -V -t f'
"$BIN" -c 'type -V -p echo'; echo "rc=$?"
echo "== -V applies to every -a resolution:"; PATH= "$BIN" -c 'type -V -a tsort'
