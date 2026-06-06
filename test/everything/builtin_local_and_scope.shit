#!/bin/sh
# Function-local variables restore the outer value on return.

outer=global
show() {
    local outer=inner
    echo "inside=$outer"
}
show
echo "outside=$outer"

# A bare local name shadows with an empty value.
probe() {
    local empty
    echo "empty=[$empty]"
}
probe

# Positional parameters are saved and restored across a function call.
set -- a b c
withargs() {
    echo "fn_args=$# $1"
}
withargs x y
echo "outer_args=$# $1"
