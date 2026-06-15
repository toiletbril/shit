#!/bin/bash
# The accepted alternative for tilde_forms.bash on a bash that resolves a bare
# tilde from the home directory captured at startup and ignores a later HOME
# reassignment (observed on nix bash 5.3.9). shit honours the reassignment, the
# behavior bash documents and other bash builds follow, so the three
# HOME-dependent lines are spelled here while the remaining tilde forms run
# unchanged and still compare live.
HOME=/usr/xyz
echo /usr/xyz
echo /usr/xyz/foo
echo /usr/xyz/foo
echo ~ch\et
echo ~ch\et/foo
echo "~chet"/"foo"
echo \~chet/"foo"
echo ~\chet/bar
echo ~chet""/bar
echo ~nosuchuserhopefully/bar
cd /tmp && cd / && echo "plus=$(cd /tmp && echo ~+)" "minus=~-"
