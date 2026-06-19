unset SHIT_FLAGS
# The shitbox utilities gained the POSIX byte and mode and almost-all flags. A
# hermetic temp directory keeps the candidates stable across machines. The
# directory is left in place rather than removed, so the test never runs rm.
dir=$(mktemp -d)
printf 'one\ntwo\nthree\nfour\n' > "$dir/f.txt"

echo "== head -c 5:"
"$BIN" -c "shitbox head -c 5 '$dir/f.txt'" </dev/null
echo ""
echo "== tail -c 6:"
"$BIN" -c "shitbox tail -c 6 '$dir/f.txt'" </dev/null
echo ""
echo "== head -n 2 still works:"
"$BIN" -c "shitbox head -n 2 '$dir/f.txt'" </dev/null
echo "== mkdir -m 700 sets the mode:"
"$BIN" -c "shitbox mkdir -m 700 '$dir/d700'" </dev/null
"$BIN" -c "shitbox ls -l '$dir'" </dev/null | grep -c '^drwx------'
echo "== ls -A lists the dot file but not . or ..:"
printf 'x' > "$dir/.hidden"
"$BIN" -c "shitbox ls -A -1 '$dir'" </dev/null
echo "== touch creates a missing file, -c leaves it missing:"
"$BIN" -c "shitbox touch '$dir/made.txt'" </dev/null
"$BIN" -c "[ -e '$dir/made.txt' ] && echo made-exists || echo made-missing" </dev/null
"$BIN" -c "shitbox touch -c '$dir/ghost.txt'" </dev/null
"$BIN" -c "[ -e '$dir/ghost.txt' ] && echo ghost-exists || echo ghost-missing" </dev/null
