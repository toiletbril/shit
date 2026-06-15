# The shitbox text utilities run against a fixed input file in a temporary
# directory, so the counts and the sorted output are the same everywhere.
unset SHIT_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
d=$(mktemp -d) || exit 1
cd "$d" || exit 1
printf 'banana\napple\ncherry\napple\n' > fruit.txt

echo "--- cat -n ---"
"$BIN" -c 'shitbox cat -n fruit.txt'
echo "--- wc ---"
"$BIN" -c 'shitbox wc fruit.txt'
echo "--- wc -l ---"
"$BIN" -c 'shitbox wc -l fruit.txt'
echo "--- head -n 2 ---"
"$BIN" -c 'shitbox head -n 2 fruit.txt'
echo "--- tail -n 1 ---"
"$BIN" -c 'shitbox tail -n 1 fruit.txt'
echo "--- sort ---"
"$BIN" -c 'shitbox sort fruit.txt'
echo "--- sort -r ---"
"$BIN" -c 'shitbox sort -r fruit.txt'
echo "--- sort then uniq -c ---"
"$BIN" -c 'shitbox sort fruit.txt | shitbox uniq -c'
echo "--- grep an ---"
"$BIN" -c 'shitbox grep an fruit.txt'
echo "--- grep -v apple ---"
"$BIN" -c 'shitbox grep -v apple fruit.txt'
echo "--- grep -i APPLE ---"
"$BIN" -c 'shitbox grep -i APPLE fruit.txt'
echo "--- tr to lower ---"
"$BIN" -c 'printf "AbC\n" | shitbox tr A-Z a-z'
echo "--- tr -d digits ---"
"$BIN" -c 'printf "a1b2c3\n" | shitbox tr -d 0-9'
echo "--- seq into head ---"
"$BIN" -c 'shitbox seq 5 | shitbox head -n 2'
echo "--- tee then read back ---"
"$BIN" -c 'shitbox seq 2 | shitbox tee tee.txt'
"$BIN" -c 'shitbox cat tee.txt'
echo "--- seq with step ---"
"$BIN" -c 'shitbox seq 2 2 8'
