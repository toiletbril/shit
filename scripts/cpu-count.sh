#!/bin/sh

is_positive_count()
{
    case $1 in
        ''|0|*[!0-9]*) return 1 ;;
        *) return 0 ;;
    esac
}

if is_positive_count "${NUMBER_OF_PROCESSORS:-}"; then
    printf '%s\n' "$NUMBER_OF_PROCESSORS"
    exit 0
fi

if command -v nproc >/dev/null 2>&1; then
    count=$(nproc 2>/dev/null)
    if is_positive_count "$count"; then
        printf '%s\n' "$count"
        exit 0
    fi
fi

if command -v sysctl >/dev/null 2>&1; then
    count=$(sysctl -n hw.logicalcpu 2>/dev/null)
    if is_positive_count "$count"; then
        printf '%s\n' "$count"
        exit 0
    fi
fi

if command -v getconf >/dev/null 2>&1; then
    count=$(getconf _NPROCESSORS_ONLN 2>/dev/null)
    if is_positive_count "$count"; then
        printf '%s\n' "$count"
        exit 0
    fi
fi

printf '1\n'
