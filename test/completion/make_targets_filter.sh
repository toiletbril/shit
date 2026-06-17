# The make completion drops a target that names a build artifact, a name that
# exists on disk, repeats a makefile name, or carries a slash, and skips a :=
# assignment, so a tab offers the phony goals rather than the artifacts. The
# bundled make parser also resolves a $(VAR) target name to its real text. PATH
# is emptied for the completing invocation so the parser answers rather than a
# system make.
case "$BIN" in /*) ;; *) BIN=$(pwd)/$BIN ;; esac
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
mkdir -p "$dir/src"
cat > "$dir/Makefile" <<'MK'
PROG := myapp
all: $(PROG)
	true
$(PROG): src/main.o
	true
install: all
	true
clean:
	true
config.status: configure
	true
src/main.o: src/main.c
	true
MK
touch "$dir/config.status" "$dir/configure" "$dir/src/main.c"
cd "$dir"
echo "== phony goals only, artifacts and assignments dropped:"
PATH=/nonexistent "$BIN" --debug-complete-at 'make ' </dev/null
