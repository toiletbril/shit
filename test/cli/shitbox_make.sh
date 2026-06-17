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

# The := immediate expansion breaks the MAKE := $(MAKE) self-reference, the
# $(wildcard) function lists files, a $(VAR:a=b) substitution reference maps
# them, an ifeq conditional selects a value, a backslash continuation joins a
# multi-line value, a prerequisite builds first, and each recipe line runs in its
# own subshell so a forking command does not abandon the line after it.
: > a.c
: > b.c
cat > Makefile <<'EOF'
MODE ?= dbg
MAKE := $(MAKE) -j4
SRC := $(wildcard *.c)
OBJ := $(SRC:%.c=%.o)
FLAGS := \
	one \
	two
ifeq ($(MODE), dbg)
TAG := debug
else
TAG := other
endif

all: dirs
	@echo "make=[$(MAKE)]"
	@echo "src=[$(SRC)]"
	@echo "obj=[$(OBJ)]"
	@echo "flags=[$(FLAGS)]"
	@echo "tag=[$(TAG)]"

dirs:
	@mkdir -p deep
	@echo "second recipe line ran after a forking command"
EOF
echo "--- advanced features ---"
"$BIN" -c 'shitbox make' 2>&1

echo "--- ignored flags -B and -k ---"
cat > Makefile <<'EOF'
all:
	@echo built
EOF
"$BIN" -c 'shitbox make -B -k all' 2>&1
echo "rc=$?"

echo "--- failing recipe aborts ---"
cat > Makefile <<'EOF'
broken:
	@false
	@echo "this line must not run"
EOF
"$BIN" -c 'shitbox make broken' 2>&1
echo "rc=$?"
