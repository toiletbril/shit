#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("target");
HELP_DESCRIPTION_DECL(
    "The assimilate builtin installs this shell on a remote SSH target.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Assimilate);

namespace shit {

namespace {

constexpr char REMOTE_TRANSACTION_TEXT[] = R"SH(set -eu
upload=$1
candidate=
backup=
target=
lock=
had_target=0
rollback_active=0
cleanup_status=0
cleanup() {
  cleanup_status=$?
  trap - 0 1 2 15
  if [ "$rollback_active" -eq 1 ]; then
    if [ "$had_target" -eq 1 ] && { [ -e "$backup" ] || [ -L "$backup" ]; }; then
      if /bin/mv -f "$backup" "$target"; then backup=; else cleanup_status=1; fi
    elif [ "$had_target" -eq 1 ]; then
      cleanup_status=1
    else
      /bin/rm -f "$target" || cleanup_status=1
    fi
  fi
  [ -z "$candidate" ] || /bin/rm -f "$candidate" || cleanup_status=1
  if [ "$rollback_active" -eq 0 ] && [ -n "$backup" ]; then
    /bin/rm -f "$backup" || cleanup_status=1
  fi
  [ -z "$lock" ] || /bin/rmdir "$lock" || cleanup_status=1
  /bin/rm -f "$upload" || cleanup_status=1
  exit "$cleanup_status"
}
trap cleanup 0
trap 'exit 129' 1
trap 'exit 130' 2
trap 'exit 143' 15
rest=${PATH-}
install_dir=
while :; do
  case $rest in
    *:*) path_entry=${rest%%:*}; rest=${rest#*:}; has_more=1 ;;
    *) path_entry=$rest; has_more=0 ;;
  esac
  [ -n "$path_entry" ] || path_entry=.
  if [ -d "$path_entry" ] && [ -w "$path_entry" ] && [ -x "$path_entry" ]; then
    install_dir=$(cd "$path_entry" && pwd -P)
    break
  fi
  [ "$has_more" -eq 1 ] || break
done
[ -n "$install_dir" ] || exit 1
candidate=$install_dir/.shit-assimilate-$2.candidate
backup=$install_dir/.shit-assimilate-$2.backup
target=$install_dir/shit
lock=$install_dir/.shit-assimilate.lock
[ ! -d "$target" ] || exit 1
/bin/cp "$upload" "$candidate"
/bin/chmod 755 "$candidate"
"$candidate" --short-version >/dev/null
/bin/mkdir "$lock"
if [ -e "$target" ] || [ -L "$target" ]; then
  /bin/cp -a "$target" "$backup"
  had_target=1
fi
rollback_active=1
/bin/mv -f "$candidate" "$target"
candidate=
"$target" --short-version >/dev/null
trap '' 1 2 15
[ "$had_target" -eq 0 ] || /bin/rm -f "$backup"
backup=
rollback_active=0
/bin/rmdir "$lock"
lock=
/bin/rm -f "$upload"
trap - 0 1 2 15
printf '%s\n' "$target"
)SH";
constexpr StringView REMOTE_TRANSACTION{REMOTE_TRANSACTION_TEXT,
                                        sizeof(REMOTE_TRANSACTION_TEXT) - 1};

fn run_program(const Path &program, ArrayList<String> arguments) throws -> i32
{
  arguments[0] = program.text();
  let const result = os::run_measured(arguments, false);
  if (!result.has_value()) return 126;

  return static_cast<i32>(result->exit_status);
}

fn resolve_transfer_program(StringView name) throws -> Maybe<Path>
{
  let const matches = utils::search_program_path(name);
  if (matches.is_empty()) return None;

  return matches[0].clone();
}

fn remote_command(StringView upload_name, StringView transaction_id,
                  Allocator allocator) throws -> String
{
  let command = String{allocator};
  append_shell_quoted_arg(command, "/bin/sh");
  command += ' ';
  append_shell_quoted_arg(command, "-c");
  command += ' ';
  append_shell_quoted_arg(command, REMOTE_TRANSACTION);
  command += ' ';
  append_shell_quoted_arg(command, "sh");
  command += ' ';
  append_shell_quoted_arg(command, upload_name);
  command += ' ';
  append_shell_quoted_arg(command, transaction_id);

  return command;
}

} /* namespace */

Assimilate::Assimilate() = default;

pure fn Assimilate::kind() const wontthrow -> Builtin::Kind
{
  return Kind::Assimilate;
}

fn Assimilate::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &arguments = ec.args();
  ASSERT(!arguments.is_empty());

  if (arguments.count() > 1 && arguments[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  bool has_invalid_target_character = false;
  if (arguments.count() == 2) {
    for (usize character_position = 0;
         character_position < arguments[1].count(); character_position++)
    {
      const char character = arguments[1][character_position];
      switch (character) {
      case ':':
      case ' ':
      case '\t':
      case '\r':
      case '\n': has_invalid_target_character = true; break;
      default: break;
      }
    }
  }

  if (arguments.count() != 2 || arguments[1].is_empty() ||
      arguments[1][0] == '-' || has_invalid_target_character)
  {
    return report_usage_error(ec, cxt, ec.program());
  }

  let const executable = os::current_executable_path();
  if (!executable.has_value()) {
    report_soft_builtin_error(ec, cxt,
                              "Cannot resolve this shell's executable path");
    return 1;
  }

  let const scp = resolve_transfer_program("scp");
  let const ssh = resolve_transfer_program("ssh");
  if (!scp.has_value() || !ssh.has_value()) {
    report_soft_builtin_error(ec, cxt, "Cannot find both scp and ssh in PATH");
    return 127;
  }

  let const allocator = cxt.scratch_allocator();
  let const transaction_id =
      String::from(static_cast<u64>(os::get_shell_process_id()), allocator) +
      "-" + String::from(os::realtime_microseconds(), allocator);
  let const upload_name =
      String{".shit-assimilate-"} + transaction_id + ".upload";
  let const destination = arguments[1].view() + ":" + upload_name;

  let scp_arguments = ArrayList<String>{allocator};
  scp_arguments.push(String{"scp"});
  scp_arguments.push(String{"--"});
  scp_arguments.push(String{*executable});
  scp_arguments.push(String{destination.view()});
  let status = run_program(*scp, steal(scp_arguments));
  if (status != 0) {
    let cleanup_arguments = ArrayList<String>{allocator};
    cleanup_arguments.push(String{"ssh"});
    cleanup_arguments.push(String{"--"});
    cleanup_arguments.push(arguments[1].clone());
    let cleanup_command = String{allocator, "/bin/rm -f "};
    append_shell_quoted_arg(cleanup_command, upload_name.view());
    cleanup_arguments.push(steal(cleanup_command));
    run_program(*ssh, steal(cleanup_arguments));
    return status;
  }

  let ssh_arguments = ArrayList<String>{allocator};
  ssh_arguments.push(String{"ssh"});
  ssh_arguments.push(String{"--"});
  ssh_arguments.push(arguments[1].clone());
  ssh_arguments.push(
      remote_command(upload_name.view(), transaction_id.view(), allocator));

  status = run_program(*ssh, steal(ssh_arguments));
  if (status != 0) {
    let cleanup_arguments = ArrayList<String>{allocator};
    cleanup_arguments.push(String{"ssh"});
    cleanup_arguments.push(String{"--"});
    cleanup_arguments.push(arguments[1].clone());
    let cleanup_command = String{allocator, "/bin/rm -f "};
    append_shell_quoted_arg(cleanup_command, upload_name.view());
    cleanup_arguments.push(steal(cleanup_command));
    run_program(*ssh, steal(cleanup_arguments));
  }

  return status;
}

} /* namespace shit */
