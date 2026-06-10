#!/bin/bash
[[ abcdef =~ (abc)(def) ]] && echo "m=${BASH_REMATCH[0]} 1=${BASH_REMATCH[1]} 2=${BASH_REMATCH[2]}"
[[ "foo123" =~ ^([a-z]+)([0-9]+)$ ]] && echo "name=${BASH_REMATCH[1]} num=${BASH_REMATCH[2]}"
[[ cat =~ ^(cat|dog)$ ]] && echo "animal=${BASH_REMATCH[1]}"
re='(a+)(b+)'
[[ aaabb =~ $re ]] && echo "var=${BASH_REMATCH[1]}-${BASH_REMATCH[2]}"
[[ "x.y" =~ "x.y" ]] && echo "quoted-literal-ok"
[[ abcdef =~ (abc)(def) ]]; [[ xyz =~ nomatch ]]; echo "after-nomatch: ${#BASH_REMATCH[@]} [${BASH_REMATCH[1]}]"
[[ xQy =~ "x.y" ]] && echo "BAD quoted dot matched" || echo "quoted-dot-literal"
[[ xay =~ x.y ]] && echo "unquoted-dot-live"
[[ "a+b" =~ a"+"b ]] && echo "BAD quoted plus" || echo "quoted-plus-literal"
[[ "a+b" =~ a+b ]] && echo "unquoted-plus-live"
[[ '^$' =~ "^$" ]] && echo "anchors-literal"
