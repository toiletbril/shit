unset SHIT_FLAGS
# A mimicked script found on PATH reads $0 as the resolved script path, the
# argument the kernel hands a shebang interpreter, so realpath "$0" finds the
# script's true directory the way envman derives its env dir.
bin=$(mktemp -d)
cat > "$bin/argv0probe" <<'EOF'
#!/bin/bash
echo "zero=$0"
test "$0" = "$BASH_SOURCE" && echo match
EOF
chmod +x "$bin/argv0probe"
PATH="$bin:$PATH" "$BIN" -I -c 'argv0probe' | sed "s|$bin|BINDIR|"
rm -rf "$bin"
echo "rc=$?"
