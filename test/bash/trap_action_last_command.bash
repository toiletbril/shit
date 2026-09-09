#!/bin/bash
# A trap action whose last command is external, fired while the script is on its
# final command. The shell has to survive the action and run the rest of the
# script. Only the final command of a file reaches the state under test.
trap '/bin/echo in-usr1-action' USR1
kill -USR1 $$
echo after-usr1
