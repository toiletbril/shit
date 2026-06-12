#!/bin/bash
# The [:name:] classes inside a bracket, across the replace, trim, [[ match,
# and case contexts, the ${cur//[[:space:]]/} construct bash-completion
# filters tokens with. A class never opens a range and an unknown class
# matches nothing.
x=" a b "
echo "strip=[${x//[[:space:]]/}]"
echo "mark=[${x//[[:space:]]/_}]"
x="a1b2c"
echo "digits=[${x//[[:digit:]]/D}]"
x="Hello World"
echo "upper=[${x//[[:upper:]]/U}]"
x="ab"
echo "trim=[${x#[[:alpha:]]}]"
[[ " " == [[:space:]] ]] && echo "cond=space"
[[ "x" == [[:alpha:]] ]] && echo "cond=alpha"
[[ "x" == [[:digit:]] ]] || echo "cond=notdigit"
case g in
  [[:digit:]]) echo "case=digit" ;;
  [[:alpha:]]) echo "case=alpha" ;;
esac
x="a-b"
echo "range_member=[${x//[a-z]/_}]"
x="abc"
echo "unknown=[${x//[[:bogus:]]/_}]"
x="]x"
echo "bracket_member=[${x//[]]/_}]"
x="tab	end"
echo "blank=[${x//[[:blank:]]/_}]"
