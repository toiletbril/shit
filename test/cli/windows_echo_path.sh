directory=
cleanup()
{
  if [ -n "$directory" ]; then
    "$TEST_SYSTEM_RM" -rf -- "$directory"
  fi
}
trap cleanup EXIT

if [ "${OS-}" = Windows_NT ]; then
  directory=$(mktemp -d) || exit 1
  path_value='C:\clear\e[2J\tail'
  output=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$path_value" \
    "$BIN" -c 'echo "$PATH"; echo survived')
  expected=$(printf '%s\n%s' "$path_value" survived)
  [ "$output" = "$expected" ] || exit 1

  output=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$path_value" \
    "$BIN" -c '# shellcheck disable=SC2123
PATH="C:\updated"; koshkit env | koshkit grep "^Path="')
  [ "$output" = 'Path=C:\updated' ] || exit 1

  output=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$path_value" \
    "$BIN" -c 'export path=first; export PATH=second; \
printf "%s %s\n" "${path@a}" "${PATH@a}"')
  [ "$output" = 'x x' ] || exit 1

  output=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$path_value" \
    MyImported=one "$BIN" --no-init-files -c \
    'printf "%s %s %s\n" "${MyImported@a}" "${MYIMPORTED@a}" "${myimported@a}"')
  [ "$output" = 'x x x' ] || exit 1

  output=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$path_value" \
    MyImported=one "$BIN" --no-init-files -c \
    'export -n MYIMPORTED; MyImported=kept; \
printf "[%s][%s]\n" "${MyImported@a}" "${MYIMPORTED@a}"')
  [ "$output" = '[][]' ] || exit 1

  output=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$path_value" \
    MyImported=one "$BIN" --no-init-files -c \
    'set -u; name=MyImportedd; echo "${!name}"' 2>&1)
  case "$output" in
    *"The variable 'MyImported' is set"*) ;;
    *) exit 1 ;;
  esac

  output=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$path_value" \
    myimported=one "$BIN" --no-init-files -c \
    'set -u; name=myimportedd; echo "${!name}"' 2>&1)
  case "$output" in
    *"The variable 'myimported' is set"*) ;;
    *) exit 1 ;;
  esac

  output=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$path_value" \
    MyImported=one "$BIN" --no-init-files -c \
    'export MYIMPORTED=two; set -u; name=MyImportedd; echo "${!name}"' 2>&1)
  case "$output" in
    *"The variable 'MyImported' is set"*) ;;
    *) exit 1 ;;
  esac

  output=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$path_value" \
    "$BIN" --no-init-files -c \
    'OtherVar=zero; (export OTHERVAR=two); printf "[%s]\n" "${OtherVar@a}"')
  [ "$output" = '[]' ] || exit 1

  printf '@echo off\r\necho path-refresh-ran\r\n' > "$directory/path-refresh.bat"
  if ! output=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$path_value" \
    "$BIN" -c 'Path="$1"; path-refresh' path-test "$directory")
  then
    exit 1
  fi
  [ "$output" = path-refresh-ran ] || exit 1

  [ "$("$BIN" --no-annoying-diagnostics -c 'printf "%s" C:\new')" = 'C:new' ] || exit 1
  [ "$("$BIN" -c "printf '%s' 'C:\new'")" = 'C:\new' ] || exit 1
  [ "$("$BIN" -c 'printf "%s" C:\\new')" = 'C:\new' ] || exit 1
  if "$BIN" --debug-highlight-at '' </dev/null >/dev/null 2>&1; then
    case "$("$BIN" --debug-highlight-at 'echo C:\Windows')" in
      *'C:\Windows'*) exit 1 ;;
    esac
  fi
fi

# The exported name store folds case on Windows and keeps it everywhere else,
# so the imported name is reachable through its uppercase spelling only there.
if [ "${OS-}" = Windows_NT ]; then
  expected_folded_lookup='[one][one]'
else
  expected_folded_lookup='[one][unset]'
fi
folded_lookup=$(MyFolded=one "$BIN" --no-init-files -c \
  'printf "[%s][%s]" "${MyFolded-unset}" "${MYFOLDED-unset}"')
[ "$folded_lookup" = "$expected_folded_lookup" ] || exit 1

[ "$("$BIN" -c 'echo -e "a\tb"')" = "$(printf 'a\tb')" ] || exit 1
echo "Exported name folding follows the platform"
echo "Windows PATH echo stays literal"
echo "Windows paths retain the portable shell escape grammar"
