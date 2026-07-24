unset SHIT_FLAGS
{
    "$BIN" -c 'local --help' 2>&1
    printf 'rc=%s\n' "$?"
} | ./normalize-trace.sh "$BIN"
