#!/bin/bash
# The tilde prefix expands only when wholly unquoted, ~+ and ~- read PWD and
# OLDPWD, and an unknown user stays literal, the lines tilde.tests runs.
HOME=/usr/xyz
echo ~
echo ~/foo
echo ~/"foo"
echo ~ch\et
echo ~ch\et/foo
echo "~chet"/"foo"
echo \~chet/"foo"
echo ~\chet/bar
echo ~chet""/bar
echo ~nosuchuserhopefully/bar
cd /tmp && cd / && echo "plus=$(cd /tmp && echo ~+)" "minus=~-"
