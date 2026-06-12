#!/bin/env bash
unset a
echo "[${a[@]-default}]"
b=(1 2 3)
echo "[${b[@]+alt}]"
echo "[${b[@]-elems}]"
unset c
echo "[${c[@]+set}]"
d=(100 200)
sparse_check() { local d; d[5]=local; }
d[40]=keep
sparse_check
echo "[${d[40]}]"
