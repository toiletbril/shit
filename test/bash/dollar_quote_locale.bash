#!/bin/bash
echo $"plain locale string"
x=world
echo $"hello $x"
echo pre$"mid"post
echo "$"
printf '[%s]\n' $"a b"
