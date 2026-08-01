#!/bin/sh

tool=$1
shift

if [ "$(uname -s)" = Darwin ]; then
    exec xcrun "$tool" "$@"
fi

exec "$tool" "$@"
