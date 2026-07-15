if false; then
    printf 'unreachable direct branch\n'
fi
printf 'direct status=%s\n' "$?"

(
    set -e
    test -f bash/fixtures/errexit_source_inner.bash &&
        . bash/fixtures/errexit_source_inner.bash
    printf 'envman source continued\n'
)
printf 'envman source status=%s\n' "$?"

(
    set -e
    false && printf 'unreachable guard branch\n'
    . /dev/null
    printf 'empty source continued\n'
)
printf 'empty source status=%s\n' "$?"

(
    set -e
    . bash/fixtures/errexit_source_outer.bash
    printf 'nested source continued\n'
)
printf 'nested source status=%s\n' "$?"

(
    set -e
    test -f bash/fixtures/errexit_source_failure.bash &&
        . bash/fixtures/errexit_source_failure.bash
    printf 'unreachable failure marker\n'
)
printf 'real failure status=%s\n' "$?"
