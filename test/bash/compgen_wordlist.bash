#!/bin/bash
compgen -W "alpha beta gamma alphabet" -- a
echo ---
compgen -W "one two three" -- t
echo ---
compgen -W "x y z" --
