#!/bin/sh

set -e

guarded_and() {
    false
    echo and-body
}
guarded_and && echo and-rhs

guarded_or() {
    false
    echo or-body
    false
}
guarded_or || echo or-rhs

guarded_not() {
    false
    echo not-body
    false
}
! guarded_not
echo after-not

inner_guarded() {
    false
    echo inner-body
}
outer_guarded() {
    inner_guarded
    echo outer-body
}
outer_guarded && echo nested-rhs

echo before-direct
direct_failure() {
    false
    echo direct-unreachable
}
direct_failure
echo after-direct
