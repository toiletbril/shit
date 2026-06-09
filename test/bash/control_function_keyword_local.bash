#!/bin/bash
# function keyword definitions, nested functions, local recursion, and return codes.

function greet {
    echo "greet $1"
    return 0
}

function pick {
    if [ "$1" -gt 5 ]; then
        return 11
    fi
    return 22
}

greet there
pick 9
echo "rc $?"
pick 2
echo "rc $?"

# local keeps recursion frames independent.
function fib {
    local n=$1
    if [ "$n" -lt 2 ]; then
        echo "$n"
        return
    fi
    local a
    local b
    a=$(fib $((n - 1)))
    b=$(fib $((n - 2)))
    echo $((a + b))
}

echo "fib 10 = $(fib 10)"

# nested function defined inside another.
function build {
    function helper {
        echo "helper got $1"
        return 0
    }
    helper "$1"
    echo "helper rc $?"
}

build cargo
echo "build rc $?"

# local does not leak to caller.
function set_local {
    local scoped=private
    echo "inside $scoped"
}
scoped=public
set_local
echo "outside $scoped"
