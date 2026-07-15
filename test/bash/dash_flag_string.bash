#!/bin/bash
flag_string=$-
echo "${flag_string//[cI]/}"
set -f
flag_string=$-
echo "${flag_string//[cI]/}"
set +f
set -u
flag_string=$-
echo "${flag_string//[cI]/}"
