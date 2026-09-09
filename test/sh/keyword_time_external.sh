#!/bin/sh
type time
echo "type=$?"
command -v time
echo "command_v=$?"
saved_path=$PATH
PATH=/nonexistent
time sleep 0 2> /dev/null
echo "missing=$?"
PATH=$saved_path
time() { echo shadowed; }
time
echo "shadowed=$?"
unset -f time
command -v time
echo "restored=$?"
echo done
