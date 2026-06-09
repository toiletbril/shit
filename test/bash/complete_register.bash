#!/bin/bash
complete -W "add commit push" mygit
echo "rc1=$?"
complete -o default -F _foo othercmd
echo "rc2=$?"
complete -W "a b" -F fn -o default name1 name2
echo "rc3=$?"
