# A bare $MANPATH leaves the macOS CommandLineTools man root out, so the git
# subcommand index misses the git-<command> pages even though they are
# installed. The manpath command is now forked through the trusted gate and
# merged into the section-1 directory list, so git subcommands complete from
# the real manpages. A hermetic fake man tree under $MANPATH keeps the
# candidates stable across machines.
dir=$(mktemp -d)
mandir="$dir/man/man1"
mkdir -p "$mandir"
cat > "$mandir/git.1" <<'EOF'
.TH GIT 1
.SH SYNOPSIS
\fBgit\fR [\fB\-v\fR]
.SH "GIT COMMANDS"
.sp
\fBgit\fR \fBfoo\fR
\fBgit\fR \fBbar\fR
EOF
cat > "$mandir/git-foo.1" <<'EOF'
.TH GIT-FOO 1
.SH SYNOPSIS
\fBgit\fR \fBfoo\fR
EOF
cat > "$mandir/git-bar.1" <<'EOF'
.TH GIT-BAR 1
.SH SYNOPSIS
\fBgit\fR \fBbar\fR
EOF
echo "== subcommands with no prefix:"
MANPATH="$dir/man" "$BIN" --debug-complete-at 'git ' </dev/null
echo "== subcommands with a prefix:"
MANPATH="$dir/man" "$BIN" --debug-complete-at 'git ba' </dev/null
rm -rf "$dir"
