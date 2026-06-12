#!/bin/bash
# shopt -p prints a command the shell replays, shopt -s or -u for a shopt name
# and set -o or +o behind the -o bridge, the $(shopt -p globstar) round-trip
# bun's completion file performs.
shopt -p globstar
shopt -p extglob nullglob
shopt -po posix
reset=$(shopt -p globstar)
shopt -s globstar
$reset
shopt globstar
shopt -p globstar
