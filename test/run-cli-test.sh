#!/bin/bash
# Run each named cli test through its own shell driver and diff the output
# against the golden. A mismatch prints and stores a unified diff in the failed
# list. The Makefile passes BIN, DIFF_FLAGS, FAILED_LIST and the cli test files
# as arguments.

test_shell=$1
shift

for f in "$@"; do
    name=$(basename "$f" .sh)
    case $name in
    command_substitution_strategy|fg_terminal_handoff)
        if [ "${IS_NONDEBUG_BUILD:-0}" = 1 ]; then
            printf "\t%-64s skipped, release binary\n" "cli/$name.sh"
            continue
        fi
        ;;
    esac
    out=$(mktemp)
    case $name in
    command_substitution_interrupt|fg_terminal_handoff|read_timeout|\
        shitbox_timeout|transaction_lock_lifetime|wait_on_stopped_job)
        golden_timeout_seconds=60
        if [ "$name" = shitbox_timeout ]; then
            golden_timeout_seconds=120
        fi
        CLI_TEST_TIMEOUT_SECONDS=${CLI_TEST_TIMEOUT_SECONDS:-$golden_timeout_seconds} \
            BIN="$BIN" "$test_shell" ./.run-bounded-cli-golden.sh "$f" \
            > "$out" 2>&1
        driver_status=$?
        if [ "$driver_status" -ne 0 ]; then
            printf 'golden exited with status %s\n' "$driver_status" >> "$out"
        fi
        ;;
    *)
        BIN="$BIN" "$test_shell" "$f" > "$out" 2>&1
        ;;
    esac
    if diff $DIFF_FLAGS "expected/cli/$name.out" "$out" >/dev/null 2>&1; then
        printf "\t%-64s ok\033[K\r" "cli/$name.sh"
    else
        diff $DIFF_FLAGS "expected/cli/$name.out" "$out" | tee -a "$FAILED_LIST"
        printf "\t%-64s FAILED :c\n" "cli/$name.sh"
    fi
    rm -f "$out"
done
