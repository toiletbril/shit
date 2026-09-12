# The koshkit usage errors carry a how-to-fix note under the located message.
# Each utility is reached through `koshkit <name>` so a binary on PATH does not
# shadow the bundled one.
unset KOSH_FLAGS

echo "=== killall arg count ==="
"$BIN" -c 'koshkit killall a b' 2>&1

echo "=== seq zero increment ==="
"$BIN" -c 'koshkit seq 1 0 10' 2>&1

echo "=== ln without -s ==="
"$BIN" -c 'koshkit ln a b' 2>&1

echo "=== tr one set ==="
"$BIN" -c 'koshkit tr abc' 2>&1

echo "=== find bad -type ==="
"$BIN" -c 'koshkit find . -type x' 2>&1

echo "=== readlink extra operand location ==="
"$BIN" -c 'koshkit readlink first second' 2>&1

echo "=== fuser conflicting flags location ==="
"$BIN" -c 'koshkit fuser -cf file' 2>&1
echo "=== whoami operand location ==="
"$BIN" -c 'koshkit whoami extra' 2>&1
echo "=== logname operand location ==="
"$BIN" -c 'koshkit logname extra' 2>&1

echo "=== nice increment location ==="
"$BIN" -c 'koshkit nice -n nope true' 2>&1
echo "=== nice command location ==="
"$BIN" -c 'koshkit nice KOSH_MISSING_COMMAND' 2>&1

echo "=== grep pattern location ==="
"$BIN" -c "koshkit grep '['" 2>&1
echo "=== grep file location ==="
"$BIN" -c 'koshkit grep value KOSH_MISSING_FILE' 2>&1

echo "=== mv source location ==="
"$BIN" -c 'koshkit mv KOSH_MISSING_SOURCE KOSH_MISSING_DESTINATION' 2>&1

echo "=== renice identifier location ==="
"$BIN" -c 'koshkit renice -n 1 nope' 2>&1

echo "=== date operand location ==="
"$BIN" -c 'koshkit date 2026' 2>&1

echo "=== getconf variable location ==="
"$BIN" -c 'koshkit getconf KOSH_MISSING_CONFIGURATION' 2>&1

echo "=== locale name location ==="
"$BIN" -c 'koshkit locale KOSH_MISSING_LOCALE' 2>&1

echo "=== id user location ==="
"$BIN" -c 'koshkit id KOSH_MISSING_USER' 2>&1

echo "=== stty setting location ==="
"$BIN" -c 'koshkit stty KOSH_MISSING_SETTING' 2>&1

echo "=== stty conflicting flag location ==="
"$BIN" -c 'koshkit stty -a -g' 2>&1

echo "=== logger priority location ==="
"$BIN" -c 'koshkit logger -p KOSH_MISSING_PRIORITY message' 2>&1

echo "=== nohup command location ==="
"$BIN" -c 'koshkit nohup KOSH_MISSING_COMMAND' 2>&1

echo "=== evil operand location ==="
"$BIN" -c 'koshkit evil extra' 2>&1

echo "=== evil color value location ==="
"$BIN" -c 'koshkit evil --color invalid' 2>&1

echo "=== evilfs operand location ==="
"$BIN" -c 'koshkit evilfs extra' 2>&1

echo "=== evillogs operand location ==="
"$BIN" -c 'koshkit evillogs extra' 2>&1

echo "=== evilnet operand location ==="
"$BIN" -c 'koshkit evilnet extra' 2>&1

echo "=== evilss operand location ==="
"$BIN" -c 'koshkit evilss extra' 2>&1

echo "=== evilps limit location ==="
"$BIN" -c 'koshkit evilps -0' 2>&1

echo "=== evilps extra operand location ==="
"$BIN" -c 'koshkit evilps 1 2' 2>&1

echo "=== evilps sort value location ==="
"$BIN" -c 'koshkit evilps --sort invalid' 2>&1

echo "=== evilps process location ==="
"$BIN" -c 'koshkit evilps invalid' 2>&1

echo "=== evilps absent process location ==="
"$BIN" -c 'koshkit evilps 9223372036854775807' 2>&1

echo "=== evilio extra operand location ==="
"$BIN" -c 'koshkit evilio 1 2' 2>&1

echo "=== evilio duration mode location ==="
"$BIN" -c 'koshkit evilio 1' 2>&1

echo "=== evilio duration value location ==="
"$BIN" -c 'koshkit evilio --cumulative=0' 2>&1

echo "=== evilio process limit location ==="
"$BIN" -c 'koshkit evilio -0' 2>&1

echo "=== evilio later count location ==="
"$BIN" -c 'koshkit evilio -5 --count 2' 2>&1

echo "=== evilio later compact limit location ==="
"$BIN" -c 'koshkit evilio --count 2 -5' 2>&1

echo "=== evilio count value location ==="
"$BIN" -c 'koshkit evilio --count 0' 2>&1

echo "=== evilio process value location ==="
"$BIN" -c 'koshkit evilio --pid 0' 2>&1

echo "=== evilio color value location ==="
"$BIN" -c 'koshkit evilio --color invalid' 2>&1

echo "=== evilio report mode location ==="
"$BIN" -c 'koshkit evilio --all --cumulative' 2>&1

echo "=== evilio later all location ==="
"$BIN" -c 'koshkit evilio --cumulative --all' 2>&1

echo "=== evilio live mode location ==="
"$BIN" -c 'koshkit evilio --all --live' 2>&1

echo "=== basename extra operand location ==="
"$BIN" -c 'koshkit basename path suffix extra' 2>&1

echo "=== bc register ranges ==="
"$BIN" -c "printf 'ibase=1\nobase=17\nscale=-1\n' | koshkit bc" 2>&1

echo "=== cal extra operand location ==="
"$BIN" -c 'koshkit cal 1 2024 extra' 2>&1

echo "=== cal month location ==="
"$BIN" -c 'koshkit cal 13 2024' 2>&1

echo "=== cal year location ==="
"$BIN" -c 'koshkit cal 1 0' 2>&1

echo "=== chgrp group location ==="
"$BIN" -c 'koshkit chgrp KOSH_MISSING_GROUP LICENSE' 2>&1

echo "=== chmod mode location ==="
"$BIN" -c 'koshkit chmod u=rz LICENSE' 2>&1

echo "=== chown owner location ==="
"$BIN" -c 'koshkit chown KOSH_MISSING_USER LICENSE' 2>&1

echo "=== chown specification location ==="
"$BIN" -c 'koshkit chown 0: LICENSE' 2>&1

echo "=== chown group location ==="
"$BIN" -c 'koshkit chown :KOSH_MISSING_GROUP LICENSE' 2>&1

echo "=== evildisk color value location ==="
"$BIN" -c 'koshkit evildisk --color invalid' 2>&1

echo "=== evildisk path location ==="
"$BIN" -c 'koshkit evildisk KOSH_MISSING_FILESYSTEM' 2>&1 >/dev/null

echo "=== evilfiles process value location ==="
"$BIN" -c 'koshkit evilfiles --pid invalid' 2>&1

echo "=== evilfiles user value location ==="
"$BIN" -c 'koshkit evilfiles --user KOSH_MISSING_USER' 2>&1

echo "=== goodfsw path location ==="
"$BIN" -c 'koshkit goodfsw KOSH_MISSING_PATH' 2>&1

echo "=== goodstat color value location ==="
"$BIN" -c 'koshkit goodstat --color invalid LICENSE' 2>&1

echo "=== goodstat path location ==="
"$BIN" -c 'koshkit goodstat KOSH_MISSING_PATH' 2>&1

echo "=== sync later filesystem flag location ==="
"$BIN" -c 'koshkit sync -d -f LICENSE' 2>&1

echo "=== sync later data flag location ==="
"$BIN" -c 'koshkit sync -f -d LICENSE' 2>&1

echo "=== sync path location ==="
"$BIN" -c 'koshkit sync KOSH_MISSING_PATH' 2>&1

echo "=== unlink extra operand location ==="
"$BIN" -c 'koshkit unlink first second' 2>&1

echo "=== unlink missing path location ==="
"$BIN" -c 'koshkit unlink KOSH_MISSING_PATH' 2>&1

echo "=== unlink directory location ==="
"$BIN" -c 'koshkit unlink .' 2>&1

echo "=== tty operand location ==="
"$BIN" -c 'koshkit tty extra' 2>&1

echo "=== uname operand location ==="
"$BIN" -c 'koshkit uname extra' 2>&1

echo "=== who first operand location ==="
"$BIN" -c 'koshkit who wrong' 2>&1

echo "=== who missing second operand location ==="
"$BIN" -c 'koshkit who am' 2>&1

echo "=== who second operand location ==="
"$BIN" -c 'koshkit who am wrong' 2>&1

echo "=== who extra operand location ==="
"$BIN" -c 'koshkit who am i extra' 2>&1

echo "=== killall missing process location ==="
"$BIN" -c 'koshkit killall KOSH_MISSING_PROCESS' 2>&1

echo "=== pkill extra pattern location ==="
"$BIN" -c 'koshkit pkill first second' 2>&1

echo "=== pkill empty pattern location ==="
"$BIN" -c 'koshkit pkill ""' 2>&1

echo "=== link extra operand location ==="
"$BIN" -c 'koshkit link first second third' 2>&1

echo "=== link destination location ==="
"$BIN" -c 'koshkit link KOSH_MISSING_SOURCE KOSH_MISSING_DESTINATION' 2>&1

echo "=== realpath operand location ==="
"$BIN" -c 'koshkit realpath KOSH_MISSING_PATH' 2>&1

echo "=== rmdir operand location ==="
"$BIN" -c 'koshkit rmdir KOSH_MISSING_DIRECTORY' 2>&1

echo "=== sleep duration location ==="
"$BIN" -c 'koshkit sleep invalid' 2>&1

echo "=== watch interval location ==="
"$BIN" -c 'koshkit watch --interval invalid true' 2>&1

echo "=== goodfsw latency location ==="
"$BIN" -c 'koshkit goodfsw --latency invalid .' 2>&1

echo "=== retry delay location ==="
"$BIN" -c 'koshkit retry --delay invalid true' 2>&1

echo "=== timeout duration location ==="
"$BIN" -c 'koshkit timeout invalid true' 2>&1

echo "=== timeout signal location ==="
"$BIN" -c 'koshkit timeout --signal KOSH_MISSING_SIGNAL 1 true' 2>&1

echo "=== timeout signal range location ==="
"$BIN" -c 'koshkit timeout --signal 999999999999999999999 1 true' 2>&1

echo "=== timeout command location ==="
"$BIN" -c 'koshkit timeout 1 KOSH_MISSING_COMMAND' 2>&1

echo "=== killall signal location ==="
"$BIN" -c 'koshkit killall --signal KOSH_MISSING_SIGNAL process' 2>&1

echo "=== pkill signal location ==="
"$BIN" -c 'koshkit pkill --signal KOSH_MISSING_SIGNAL process' 2>&1
