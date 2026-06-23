#!/bin/bash
# Benchmark configure.sh, configure.bash, and configure.shit across the reference
# shells and shit, reporting wall-clock seconds at the given scale and checking
# that shit output matches the reference shell. The Makefile passes SCALE, BIN,
# DASH, BASH, ZSH, ASH, YASH, BENCH, BENCH_BASH, and BENCH_SHIT. Run from the
# test directory. The bash time keyword formats the wall clock through
# TIMEFORMAT.

export TIMEFORMAT="  %R"
scale="$SCALE"

d=/tmp/shit_bench_d; b=/tmp/shit_bench_b; z=/tmp/shit_bench_z
g=/tmp/shit_bench_g; l=/tmp/shit_bench_l; s=/tmp/shit_bench_s
bb=/tmp/shit_bench_bb; sb=/tmp/shit_bench_sb
zb=/tmp/shit_bench_zb; zs=/tmp/shit_bench_zs

echo "configure.sh, wall-clock seconds at SCALE=$scale, lower is better:"
printf "  %-16s" "$DASH"; ( time SCALE=$scale $DASH $BENCH >"$d" 2>&1 ) 2>&1
printf "  %-16s" "$BASH"; ( time SCALE=$scale $BASH $BENCH >"$b" 2>&1 ) 2>&1
printf "  %-16s" "$ZSH";  ( time SCALE=$scale $ZSH  $BENCH >"$z" 2>&1 ) 2>&1
printf "  %-16s" "$ASH";  ( time SCALE=$scale $ASH $BENCH >"$g" 2>&1 ) 2>&1
printf "  %-16s" "$YASH"; ( time SCALE=$scale $YASH $BENCH >"$l" 2>&1 ) 2>&1
printf "  %-16s" "$BIN";  ( time SCALE=$scale $BIN --mood sh $BENCH >"$s" 2>&1 ) 2>&1
if cmp "$d" "$s"; then echo "output matches dash"
else echo "output differs from dash:"; diff "$d" "$s" | head -20; fi
printf "  %-16s" "$BIN+opt"; ( time SCALE=$scale $BIN $BENCH >"$s" 2>/dev/null ) 2>&1
if cmp "$d" "$s"; then echo "output matches dash with the optimizer on"
else echo "output differs from dash:"; diff "$d" "$s" | head -20; fi

echo "configure.bash, wall-clock seconds at SCALE=$scale, lower is better:"
printf "  %-16s" "$BASH"; ( time SCALE=$scale $BASH $BENCH_BASH >"$bb" 2>&1 ) 2>&1
printf "  %-16s" "$BIN";  ( time SCALE=$scale $BIN --mood bash $BENCH_BASH >"$sb" 2>&1 ) 2>&1
if cmp "$bb" "$sb"; then echo "output matches bash"
else echo "output differs from bash:"; diff "$bb" "$sb" | head -20; fi
printf "  %-16s" "$BIN+opt"; ( time SCALE=$scale $BIN $BENCH_BASH >"$sb" 2>/dev/null ) 2>&1
if cmp "$bb" "$sb"; then echo "output matches bash with the optimizer on"
else echo "output differs from bash:"; diff "$bb" "$sb" | head -20; fi

echo "configure.shit, wall-clock seconds at SCALE=$scale, lower is better:"
printf "  %-16s" "$BASH"; ( time SCALE=$scale $BASH $BENCH_SHIT >"$zb" 2>&1 ) 2>&1
printf "  %-16s" "$BIN+opt"; ( time SCALE=$scale $BIN $BENCH_SHIT >"$zs" 2>/dev/null ) 2>&1
if cmp "$zb" "$zs"; then echo "output matches bash with the optimizer on"
else echo "output differs from bash:"; diff "$zb" "$zs" | head -20; fi

rm -f "$d" "$b" "$z" "$g" "$l" "$s" "$bb" "$sb" "$zb" "$zs"
