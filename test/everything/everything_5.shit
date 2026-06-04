#!/bin/sh
# Heredocs with expansion, a quoted delimiter, tab stripping, and a pipeline.

name=world
cat <<EOF
Hello, $name!
Sum is $((3 + 4)).
EOF

cat <<'LITERAL'
No $expansion happens in here.
LITERAL

echo "smallest:"
sort <<DATA | head -n1
gamma
alpha
beta
DATA

cat <<-INDENTED
	tab-stripped line
	another one
INDENTED
