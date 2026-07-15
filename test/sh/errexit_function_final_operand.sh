#!/bin/sh

set -e

final_failure() {
    false
    echo final-unreachable
}

echo before-final
true && final_failure
echo after-final
