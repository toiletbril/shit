#!/bin/bash
echo "dec: $((10#15))"
echo "hex: $((16#ff))"
echo "bin: $((2#101))"
echo "octbase: $((8#17))"
echo "b36: $((36#z))"
echo "lead0: $((10#0042))"
echo "mixed: $((16#A + 2#10))"
u=42.9
echo "trunc: $((10#0${u%.*}))"
