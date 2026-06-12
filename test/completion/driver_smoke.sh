# The driver lists command-position candidates for a prefix and respects a
# registered spec, the engine end to end without a tty.
echo "== command prefix:"
"$BIN" --debug-complete-at 'expor' </dev/null
echo "== spec words filter by prefix:"
"$BIN" -c 'complete -W "fetch fold format" gadget' --debug-complete-at 'gadget f' </dev/null
