#!/bin/bash
[[ abc == a*c ]] && echo glob-yes || echo glob-no
[[ abc == "a*c" ]] && echo lit-yes || echo lit-no
[[ "a*c" == "a*c" ]] && echo exact-yes || echo exact-no
[[ "a*c" == a*c ]] && echo gmatch-yes || echo gmatch-no
[[ hello.txt == "*.txt" ]] && echo q-yes || echo q-no
[[ hello.txt == *.txt ]] && echo u-yes || echo u-no
p='a*c'
[[ abc == $p ]] && echo var-yes || echo var-no
[[ abc != "a*c" ]] && echo ne-yes || echo ne-no
