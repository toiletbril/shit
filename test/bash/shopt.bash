#!/bin/bash
# Bash shopt builtin, checked byte-for-byte against bash. Sets, unsets, and
# queries the shell options and the -q exit status. The display line's padding
# width differs between bash builds, so the listing runs are squeezed through
# tr and compare on content rather than the column.
shopt -s extglob
shopt extglob | tr -s ' \t' ' '
shopt -u extglob
shopt extglob | tr -s ' \t' ' '
shopt -s globstar nullglob dotglob
shopt globstar | tr -s ' \t' ' '
shopt nullglob | tr -s ' \t' ' '
shopt dotglob | tr -s ' \t' ' '
shopt -q extglob
echo "extglob query: $?"
shopt -s extglob
shopt -q extglob
echo "after set: $?"
shopt -u extglob
shopt -q extglob
echo "after unset: $?"
shopt -s nocaseglob nocasematch
shopt nocaseglob | tr -s ' \t' ' '
shopt nocasematch | tr -s ' \t' ' '
