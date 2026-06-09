#!/bin/sh
# Special builtins keep their prefix assignments after the command, and a
# redirection error fails a regular command but does not abort the shell,
# checked against dash. A non-special command's prefix assignment stays
# temporary.

# A prefix assignment before a special builtin persists, unexported.
x=2 eval ":"
echo "persist=[$x]"
sh -c 'echo "child=[$x]"'

# A prefix before a regular builtin does not persist.
y=9 true
echo "temporary=[$y]"

# export and readonly are special, so their prefixes persist too.
foo=bar export baz=1
echo "export_prefix=[$foo][$baz]"

# A redirection that cannot open its target fails the command but the shell
# continues to the next one.
true > /no/such/directory/file 2>/dev/null
echo "after_bad_redirect=$?"
echo still_running
