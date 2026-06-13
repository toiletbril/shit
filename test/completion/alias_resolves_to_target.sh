# Completing an alias consults the spec, subcommands, and options of the
# command it expands to, so g aliased to git completes git's subcommands and
# options rather than nothing.
echo "== aliased subcommands:"
"$BIN" -c 'alias g=git' --debug-complete-at 'g check-ref' </dev/null
echo "== aliased options:"
"$BIN" -c 'alias g=git' --debug-complete-at 'g --vers' </dev/null
