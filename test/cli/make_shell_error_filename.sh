unset SHIT_FLAGS
# A command that fails inside a $(shell ...) of a shitbox makefile reports the
# error with the make filename rather than a bare unnamed line. The temp
# directory is left in place so the test never runs rm.
dir=$(mktemp -d)
printf 'V := $(shell nonexistent_prog_zzz)\nall:\n\techo $(V)\n' > "$dir/Makefile"
echo "== the \$(shell) error names the make source (count):"
"$BIN" -c "cd '$dir'; shitbox make" 2>&1 | grep -c "make:.*Program 'nonexistent_prog_zzz' wasn't found"
