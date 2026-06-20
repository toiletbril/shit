#!/bin/bash
# kill -s names the signal by name and -n by number, apart from the -SIGNAL
# shorthand, while signal 0 only checks the target exists. A trapped self-signal
# proves the name resolves without any process dying.
trap 'echo caught-usr1' USR1
kill -s USR1 $$
echo "exists_s=$(kill -s 0 $$; echo $?)"
echo "exists_n=$(kill -n 0 $$; echo $?)"
