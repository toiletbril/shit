#!/bin/bash
# Bash case modification with a glob bracket class selector, checked
# byte-for-byte against bash. The ^^ and ,, forms restrict the conversion to the
# characters that match the trailing pattern.
upper=ABCDEF
echo "${upper,,[ACE]}"
lower=abcdef
echo "${lower^^[bdf]}"
word=banana
echo "${word^^a}"
echo "${word^[b]}"
mixed=AaBbCc
echo "${mixed,,[A-Z]}"
echo "${mixed^^[a-z]}"
