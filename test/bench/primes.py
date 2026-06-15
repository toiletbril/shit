#!/usr/bin/env python3
"""Segmented Sieve of Eratosthenes.
Usage: sieve.py [LIMIT]   (omit LIMIT to run until interrupted)"""
import sys

limit = int(sys.argv[1]) if len(sys.argv) > 1 else None
seg = 50000
primes = []
lo = 2

while True:
    hi = lo + seg - 1
    if limit is not None and hi > limit:
        hi = limit
    if hi < lo:           # limit < 2: nothing to emit
        break

    comp = bytearray(hi - lo + 1)   # 0 = prime, 1 = composite; index n - lo

    # mark using already-known base primes
    for p in primes:
        if p * p > hi:
            break
        start = ((lo + p - 1) // p) * p
        if start < p * p:
            start = p * p
        for m in range(start, hi + 1, p):
            comp[m - lo] = 1

    # scan segment; in-segment marking seeds the very first block
    for n in range(lo, hi + 1):
        if not comp[n - lo]:
            primes.append(n)
            print(n)
            if n * n <= hi:
                for m in range(n * n, hi + 1, n):
                    comp[m - lo] = 1

    if limit is not None and hi >= limit:
        break
    lo = hi + 1
