#!/bin/bash
printf '%q\n' "hello world"
printf '%q\n' "it's"
printf '%q\n' ""
printf '%q\n' 'a$b'
printf '%q\n' 'plain_text'
printf '%q\n' 'a"b'
printf '%q\n' 'x;y|z&w'
printf '%q\n' '(paren)'
printf '%q\n' 'a=b:c,d.e/f@g%h+i-j'
printf '%q\n' '~tilde'
printf '%q\n' '#hash'
printf '%q\n' $'tab\there'
printf '%q\n' $'ctrl\x01end'
printf '%q %q\n' foo bar
