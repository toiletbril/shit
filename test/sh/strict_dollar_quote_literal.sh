#!/bin/sh
# In POSIX mode the dollar before a double quote stays a literal dollar, the
# bash locale-string meaning is suppressed, matching dash.
echo $"x"
v=val
echo $"a $v b"
