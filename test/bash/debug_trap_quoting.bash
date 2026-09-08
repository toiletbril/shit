#!/bin/bash
# shellcheck disable=SC2034,SC2086,SC2249

echo words
trap 'echo "D-$BASH_COMMAND"' DEBUG
echo plain
echo "double $HOME"
echo 'single $HOME'
echo a\ b
echo "mixed"'and'more
trap - DEBUG

echo assignments
trap 'echo "D-$BASH_COMMAND"' DEBUG
quoted_value='a b'
prefix_value=one echo prefixed
substituted_value=$( echo body )
spaced_value=$(echo  body  again)
trap - DEBUG

echo substitutions
trap 'echo "D-$BASH_COMMAND"' DEBUG
echo captured $(echo tight)
echo captured $( echo padded )
echo captured $(  echo  padded  wide  )
echo captured `echo  tick`
echo captured "$( echo quoted )"
echo captured "before  $( echo inner )  after"
echo nested $( echo $( echo  deep ) )
echo arith $(( 1  +  2 ))
echo braced ${HOME}
echo joined x$( echo  glued )y
trap - DEBUG

echo spacing
trap 'echo "D-$BASH_COMMAND"' DEBUG
echo  two   three
echo	tab	between
echo continued \
  operand
echo "kept  blanks" and 'kept  more'
echo one
trap - DEBUG

echo nested-trap
trap 'echo "D-$BASH_COMMAND"' DEBUG
trap 'echo E-nothing' EXIT
trap - EXIT
trap - DEBUG

echo redirections
trap 'echo "D-$BASH_COMMAND"' DEBUG
echo file > /dev/null
echo tight>/dev/null
echo numbered 1> /dev/null
echo append >> /dev/null
echo override >| /dev/null
echo dup 2>&1 > /dev/null
echo close 3>&- > /dev/null
echo both &> /dev/null
echo bothappend &>> /dev/null
prefix_redirect=one echo prefixed > /dev/null
> /dev/null echo leading
cat <<< 'here  string' > /dev/null
cat 0<> /dev/null
trap - DEBUG

echo heredocs
trap 'echo "D-$BASH_COMMAND"' DEBUG
cat <<EOF > /dev/null
first
second
EOF
cat <<-'END' > /dev/null
	tabbed
	END
trap - DEBUG

echo allocations
exec {named_fd}<> /dev/null
trap 'echo "D-$BASH_COMMAND"' DEBUG
echo named >&$named_fd
trap - DEBUG
exec {named_fd}>&-

echo function-body
sample_function() {
  echo 'inside $HOME'
}
set -T
trap 'echo "D-$BASH_COMMAND"' DEBUG
sample_function
trap - DEBUG
set +T

echo done
