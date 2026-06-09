#!/bin/sh
# POSIX function definitions, nested functions, return with explicit codes and $?.

greet() {
    echo "hello $1"
    return 0
}

classify() {
    if [ "$1" -gt 10 ]; then
        return 3
    fi
    return 7
}

greet world
classify 20
echo "rc $?"
classify 5
echo "rc $?"

outer_fn() {
    inner_fn() {
        echo "inner sees $1"
        return 42
    }
    inner_fn "$1"
    echo "inner rc $?"
    return 0
}

outer_fn payload
echo "outer rc $?"

# Recursive factorial computing a deterministic value.
fact() {
    if [ "$1" -le 1 ]; then
        echo 1
        return
    fi
    prev=$(fact $(($1 - 1)))
    echo $(($1 * prev))
}

echo "fact 5 = $(fact 5)"
echo "fact 6 = $(fact 6)"

# return value of last command propagates when return has no argument.
last_status() {
    false
    return
}
last_status
echo "implicit rc $?"
