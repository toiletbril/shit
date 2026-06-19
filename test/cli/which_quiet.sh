unset SHIT_FLAGS
# which -q prints nothing and reports only through the status.
echo "== -q on a resolving name is silent, status 0:"
"$BIN" -c 'shitbox which -q sh; echo "status=$?"' 2>&1
echo "== -q on a missing name is silent, status 1:"
"$BIN" -c 'shitbox which -q no_such_program_zzz; echo "status=$?"' 2>&1
echo "== without -q the location still prints:"
"$BIN" -c 'shitbox which sh' 2>&1 | grep -c '/sh$'
