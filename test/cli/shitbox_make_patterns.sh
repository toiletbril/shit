# The barebones make resolves a pattern rule when no explicit rule names the
# goal, deriving the stem from the % and filling the automatic variables $@, $<,
# and $^ in the recipe.
unset SHIT_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
d=$(mktemp -d) || exit 1
cd "$d" || exit 1

printf 'x\n' > foo.c
printf 'y\n' > bar.c

cat > Makefile <<'EOF'
CC = echo cc

all: foo.o bar.o

%.o: %.c
	$(CC) $< -o $@
EOF

echo "--- default goal builds both through the pattern ---"
"$BIN" -c 'shitbox make'
echo "--- explicit object target ---"
"$BIN" -c 'shitbox make foo.o'
echo "--- all prerequisites variable ---"
cat > Makefile <<'EOF'
report: a b c
	echo all are $^ and first is $<
EOF
: > a
: > b
: > c
"$BIN" -c 'shitbox make'
