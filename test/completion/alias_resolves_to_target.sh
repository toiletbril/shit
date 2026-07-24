echo "== aliased subcommands:"
"$BIN" -c 'complete -W "check-ref-format --version" target; alias g=target' \
    --debug-complete-at 'g check-ref' </dev/null
echo "== aliased options:"
"$BIN" -c 'complete -W "check-ref-format --version" target; alias g=target' \
    --debug-complete-at 'g --vers' </dev/null
