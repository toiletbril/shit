#!/bin/bash
printf '%s' $'\cA\cB\cC\cI\cJ' | od -An -tx1 | tr -s ' '
printf '%s' $'\c[\c\\\c]' | od -An -tx1 | tr -s ' '
printf '%s' $'\cm\cz\cZ' | od -An -tx1 | tr -s ' '
printf 'after=%s\n' $'\cGdone'
printf '%s' $'\c1\c0\c9' | od -An -tx1 | tr -s ' '
printf '%s' $'\c!\c~\c}\c@' | od -An -tx1 | tr -s ' '
printf '%s' $'\c?' | od -An -tx1 | tr -s ' '
