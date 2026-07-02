#!/bin/bash
# The positional parameters take the ${@:-word} default, the ${@:+word}
# alternate, and the ${@:=word} pass-through the way a scalar does, distinct
# from the ${@:offset} slice. A colon test treats an empty single parameter as
# null, while two parameters stay non-null even when empty. The ${@: -1} slice
# with a leading space and the ${@:1:2} length form still run as slices. The
# fatal ${@:?} and the null ${@:=} are fenced in subshells so the run goes on.
set --
printf '<%s>' "${@:-DEF}"; echo
set -- ""
printf '<%s>' "${@:-DEF}"; echo
set -- "" ""
printf '<%s>' "${@:-DEF}"; echo
set -- a b
printf '<%s>' "${@:-DEF}"; echo
set --
printf '<%s>' ${@:-a b c}; echo
set --
printf '<%s>' "${*:-DEF}"; echo
set -- a b
printf '<%s>' "${*:-DEF}"; echo
set --
printf '<%s>' "${@:+ALT}"; echo
set -- ""
printf '<%s>' "${@:+ALT}"; echo
set -- "" ""
printf '<%s>' "${@:+ALT}"; echo
set -- a b
printf '<%s>' "${@:+ALT}"; echo
set -- a b
printf '<%s>' ${@:+one two}; echo
set --
printf '<%s>' "${@-DEF}"; echo
set -- a b
printf '<%s>' "${@-DEF}"; echo
set -- a b
printf '<%s>' "${@:=x}"; echo
set -- a b c
echo "${@: -1}"
echo "${@:1:2}"
set -- a b c d
echo "${@:2}"
set -- a b
echo "have=${@:?should not fire}"
