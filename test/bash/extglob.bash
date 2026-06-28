#!/bin/bash
# Bash extended globs, checked byte-for-byte against bash. The groups ?(..),
# *(..), +(..), @(..), and !(..) match in the [[ ]] pattern, in a case label, in
# a parameter expansion, and against filenames. bash needs extglob set before it
# parses, so the option is set first.
shopt -s extglob
[[ abc == @(abc|xyz) ]] && echo at-one
[[ abcabc == +(abc) ]] && echo plus
[[ color == c?(olo)r ]] && echo opt
[[ aXXc == a*(X)c ]] && echo star
[[ foo == !(bar) ]] && echo neg
[[ bar == @(x|y) ]] || echo no-match
v=foobar
echo "${v##+(fo)}"
f=image.jpg
echo "${f%%@(.jpg|.png)}"
case hello in @(hi|hello)) echo case-yes;; esac
case zzz in !(a|b)) echo case-neg;; esac
dir=/tmp/shit_extglob_test_$$
rm -rf "$dir"; mkdir -p "$dir"
( cd "$dir" && touch a.txt b.txt c.log foo bar )
( cd "$dir" && echo @(a|b).txt )
( cd "$dir" && echo @(foo|bar) )
( cd "$dir" && echo !(*.txt) )
rm -rf "$dir"
