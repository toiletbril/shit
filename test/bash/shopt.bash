#!/bin/bash
# Bash shopt builtin, checked byte-for-byte against bash. Sets, unsets, and
# queries the shell options, with the padded display line and the -q exit
# status. The pattern engines for extglob and globstar are not wired yet, so the
# option only records its state here.
shopt -s extglob
shopt extglob
shopt -u extglob
shopt extglob
shopt -s globstar nullglob dotglob
shopt globstar
shopt nullglob
shopt dotglob
shopt -q extglob
echo "extglob query: $?"
shopt -s extglob
shopt -q extglob
echo "after set: $?"
shopt -u extglob
shopt -q extglob
echo "after unset: $?"
shopt -s nocaseglob nocasematch
shopt nocaseglob
shopt nocasematch
