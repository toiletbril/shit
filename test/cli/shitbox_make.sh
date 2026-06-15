# The barebones make reads a Makefile, expands its variables, and runs each
# recipe through the shell. The recipe lines echo before they run, and a @ prefix
# silences that echo.
unset SHIT_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
d=$(mktemp -d) || exit 1
cd "$d" || exit 1

cat > Makefile <<'EOF'
CC = echo
GREETING = hello

all: greet done_marker

greet:
	$(CC) $(GREETING) from make

done_marker:
	@$(CC) finished
EOF

echo "--- default goal ---"
"$BIN" -c 'shitbox make'
echo "--- explicit target ---"
"$BIN" -c 'shitbox make greet'
echo "--- missing target ---"
"$BIN" -c 'shitbox make nope' 2>&1
echo "rc=$?"
