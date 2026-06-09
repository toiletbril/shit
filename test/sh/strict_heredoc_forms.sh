#!/bin/sh
# The here-document forms, checked against dash. An unquoted delimiter expands a
# parameter and a command substitution in the body, a quoted delimiter keeps the
# body literal, and the dash form strips leading tabs from the body and the
# delimiter line.

name=world
count=3

# An unquoted delimiter expands inside the body.
cat <<EOF
hello $name
sum is $((count + 4))
sub is $(echo nested)
EOF

# A quoted delimiter keeps the body literal with no expansion.
cat <<'LITERAL'
no $name here
no $((1 + 1)) here
LITERAL

# The dash form strips a leading tab from each body line.
cat <<-TABBED
	first
	second
TABBED

# A here-document feeds a pipeline stage.
sort <<DATA | head -n2
cherry
apple
banana
DATA
