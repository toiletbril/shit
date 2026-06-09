#!/bin/bash
v=hello
echo "${v~}"
echo "${v~~}"
w=WORLD
echo "${w~}"
echo "${w~~}"
m=MixedCase
echo "${m~~}"
echo "${m~}"
n=123abc
echo "${n~~}"
