#!/bin/bash
# Run each named cli test through its own shell driver and diff the output
# against the golden. A mismatch appends a unified diff to the failed list. The
# Makefile passes BIN, DIFF_FLAGS, FAILED_LIST and the cli test files as
# arguments.

for f in "$@"; do
    name=$(basename "$f" .sh)
    out=$(mktemp)
    case $name in
    command_substitution_interrupt|shitbox_timeout|transaction_lock_lifetime)
        fixture_timeout_seconds=60
        if [ "$name" = shitbox_timeout ]; then
            fixture_timeout_seconds=120
        fi
        CLI_TEST_TIMEOUT_SECONDS=${CLI_TEST_TIMEOUT_SECONDS:-$fixture_timeout_seconds} \
            BIN=$BIN ./.run-bounded-cli-fixture.sh "$f" > "$out" 2>&1
        driver_status=$?
        if [ "$driver_status" -ne 0 ]; then
            printf 'fixture exited with status %s\n' "$driver_status" >> "$out"
        fi
        ;;
    *)
        BIN=$BIN sh "$f" > "$out" 2>&1
        ;;
    esac
    if diff $DIFF_FLAGS "expected/cli/$name.out" "$out" >/dev/null 2>&1; then
        printf "\t%-64s ok\033[K\r" "cli/$name.sh"
    else
        diff $DIFF_FLAGS "expected/cli/$name.out" "$out" >> "$FAILED_LIST"
        printf "\t%-64s FAILED :c\n" "cli/$name.sh"
    fi
    rm -f "$out"
done
