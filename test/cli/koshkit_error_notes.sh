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
