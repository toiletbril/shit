#!/bin/bash
printf '%s' $'\cA\cB\cC\cI\cJ' | od -An -tx1 | tr -s ' '
printf '%s' $'\c[\c\\\c]' | od -An -tx1 | tr -s ' '
printf '%s' $'\cm\cz\cZ' | od -An -tx1 | tr -s ' '
printf 'after=%s\n' $'\cGdone'
