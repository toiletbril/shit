#!/usr/bin/env bash
# Segmented Sieve of Eratosthenes.
# Usage: sieve [LIMIT]   (omit LIMIT to run until interrupted)

limit=${1:-}
seg=50000
primes=()
lo=2

while :; do
    hi=$((lo + seg - 1))
    if [[ -n $limit && $hi -gt $limit ]]; then
        hi=$limit
    fi

    declare -A comp
    # mark using already-known base primes
    for p in "${primes[@]}"; do
        (( p * p > hi )) && break
        start=$(( (lo + p - 1) / p * p ))
        (( start < p * p )) && start=$((p * p))
        for (( m = start; m <= hi; m += p )); do
            comp[$m]=1
        done
    done

    # scan segment; in-segment marking seeds the very first block
    for (( n = lo; n <= hi; n++ )); do
        if [[ -z ${comp[$n]} ]]; then
            primes+=("$n")
            printf '%s\n' "$n"
            if (( n * n <= hi )); then
                for (( m = n * n; m <= hi; m += n )); do
                    comp[$m]=1
                done
            fi
        fi
    done

    unset comp
    if [[ -n $limit && $hi -ge $limit ]]; then
        break
    fi
    lo=$((hi + 1))
done
