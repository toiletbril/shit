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
target=
lock=
lock_published=0
had_target=0
rollback_active=0
committed=0
cleanup() {
  transaction_status=${1-$?}
  cleanup_failed=0
  trap - 0 1 2 15
  if [ -n "$lock" ]; then
    [ -n "$candidate" ] || [ ! -r "$lock/candidate" ] || candidate=$(shitbox cat "$lock/candidate")
    [ -n "$upload" ] || [ ! -r "$lock/upload" ] || upload=$(shitbox cat "$lock/upload")
    if [ "$rollback_active" -eq 1 ] && [ "$committed" -eq 0 ]; then
      if [ "$had_target" -eq 1 ] && { [ -e "$backup" ] || [ -L "$backup" ]; }; then
        shitbox mv -f "$backup" "$target" || cleanup_failed=1
      elif [ "$had_target" -eq 1 ]; then
        [ -e "$target" ] || [ -L "$target" ] || cleanup_failed=1
      else
        shitbox rm -f "$target" || cleanup_failed=1
      fi
    fi
    [ -z "$candidate" ] || shitbox rm -f "$candidate" || cleanup_failed=1
    shitbox rm -f "$upload" || cleanup_failed=1
    if [ "$committed" -eq 1 ] || [ "$rollback_active" -eq 0 ]; then
      shitbox rm -f "$backup" || cleanup_failed=1
    fi
    if [ "$cleanup_failed" -eq 0 ]; then
      shitbox rm -f "$lock/upload" "$lock/candidate" \
        "$lock/had-target" "$lock/rollback" "$lock/committed"
      [ "$lock_published" -eq 0 ] || shitbox rm -f "$lock_path" || cleanup_failed=1
      if [ "$cleanup_failed" -eq 0 ]; then
        shitbox rm -f "$lock/owner"
        shitbox rmdir "$lock" || cleanup_failed=1
      fi
    fi
  else
    [ -z "$candidate" ] || shitbox rm -f "$candidate" || cleanup_failed=1
    shitbox rm -f "$upload" || cleanup_failed=1
  fi
  [ "$cleanup_failed" -eq 0 ] || exit 1
  exit "$transaction_status"
}
recover_stale_lock() {
  lock_record=$(shitbox cat "$lock_path")
  stale_lock_name=${lock_record#*:}
  case $stale_lock_name in
    .shit-assimilate-*.lock)
      case $stale_lock_name in */*) exit 1 ;; esac
      ;;
    *) exit 1 ;;
  esac
  stale_lock=$install_dir/$stale_lock_name
  stale_backup=$stale_lock/backup
  stale_upload=
  stale_candidate=
  [ ! -r "$stale_lock/upload" ] || stale_upload=$(shitbox cat "$stale_lock/upload")
  [ ! -r "$stale_lock/candidate" ] || stale_candidate=$(shitbox cat "$stale_lock/candidate")
  case $stale_upload in
    .shit-assimilate-*.upload)
      case $stale_upload in */*) stale_upload= ;; esac
      ;;
    *) stale_upload= ;;
  esac
  case $stale_candidate in
    "$install_dir"/.shit-assimilate-*.candidate)
      stale_candidate_name=${stale_candidate#"$install_dir"/}
      case $stale_candidate_name in */*) stale_candidate= ;; esac
      ;;
    *) stale_candidate= ;;
  esac
  if [ ! -e "$stale_lock/committed" ] && [ -e "$stale_lock/rollback" ]; then
    if [ -e "$stale_lock/had-target" ]; then
      if [ -e "$stale_backup" ] || [ -L "$stale_backup" ]; then
        shitbox mv -f "$stale_backup" "$target"
      else
        [ -e "$target" ] || [ -L "$target" ]
      fi
    else
      shitbox rm -f "$target"
    fi
  fi
  [ -z "$stale_candidate" ] || shitbox rm -f "$stale_candidate"
  [ -z "$stale_upload" ] || shitbox rm -f "$stale_upload"
  shitbox rm -f "$stale_backup"
  shitbox rm -f "$stale_lock/upload" "$stale_lock/candidate" "$stale_lock/had-target" \
    "$stale_lock/rollback" "$stale_lock/committed"
  shitbox rmdir "$stale_lock/recovering"
  shitbox rm -f "$lock_path"
  shitbox rm -f "$stale_lock/owner"
  shitbox rmdir "$stale_lock"
}
lock_owner_is_live() {
  owner=
  [ -r "$lock_path" ] || return 1
  owner=$(shitbox cat "$lock_path")
  owner=${owner%%:*}
  case $owner in
    ''|*[!0-9]*) return 0 ;;
  esac
  kill -0 "$owner" 2>&-
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
target=$install_dir/shit
lock_path=$install_dir/.shit-assimilate.lock
[ "${3-}" != recover ] || {
  if [ -e "$lock_path" ]; then
    if lock_owner_is_live; then
      cleanup 1
    fi
    lock_record=$(shitbox cat "$lock_path")
    stale_lock_name=${lock_record#*:}
    case $stale_lock_name in
      .shit-assimilate-*.lock) ;;
      *) cleanup 1 ;;
    esac
    shitbox mkdir "$install_dir/$stale_lock_name/recovering" 2>&- || exit 1
    recover_stale_lock
  fi
  shitbox rm -f "$upload"
  trap - 0 1 2 15
  exit 0
}
[ ! -d "$target" ] || exit 1
trap '' 1 2 15
lock=$install_dir/.shit-assimilate-$2.lock
shitbox mkdir "$lock"
backup=$lock/backup
printf '%s:%s\n' "$$" "${lock##*/}" > "$lock/owner"
printf '%s\n' "$upload" > "$lock/upload"
printf '%s\n' "$candidate" > "$lock/candidate"
if ! shitbox ln -s "$lock/owner" "$lock_path"; then
  if lock_owner_is_live; then
    cleanup 1
  fi
  lock_record=$(shitbox cat "$lock_path")
  stale_lock_name=${lock_record#*:}
  case $stale_lock_name in
    .shit-assimilate-*.lock) ;;
    *) cleanup 1 ;;
  esac
  shitbox mkdir "$install_dir/$stale_lock_name/recovering" 2>&- || exit 1
  recover_stale_lock
  shitbox ln -s "$lock/owner" "$lock_path"
fi
lock_published=1
trap 'exit 129' 1
trap 'exit 130' 2
trap 'exit 143' 15
shitbox mv "$upload" "$candidate"
if ! candidate_version=$("$candidate" --short-version); then
  cleanup 1
fi
if [ -e "$target" ] || [ -L "$target" ]; then
  had_target=1
  : > "$lock/had-target"
fi
rollback_active=1
: > "$lock/rollback"
[ "$had_target" -eq 0 ] || shitbox mv -f "$target" "$backup"
shitbox mv -f "$candidate" "$target"
candidate=
if ! target_version=$("$target" --short-version); then
  cleanup 1
fi
trap '' 1 2 15
committed=1
: > "$lock/committed"
rollback_active=0
shitbox rm -f "$lock/rollback"
[ "$had_target" -eq 0 ] || shitbox rm -f "$backup"
shitbox rm -f "$lock/upload" "$lock/candidate" \
  "$lock/had-target" "$lock/committed"
shitbox rm -f "$lock_path"
shitbox rm -f "$lock/owner"
shitbox rmdir "$lock"
lock=
shitbox rm -f "$upload"
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
                  bool recovery_only, Allocator allocator) throws -> String
{
  let command = String{allocator};
  command += "exec ";
  let const upload_path = String{allocator, "./"} + upload_name;
  append_shell_quoted_arg(command, upload_path.view());
  command += ' ';
  append_shell_quoted_arg(command, "-p");
  command += ' ';
  append_shell_quoted_arg(command, "--mood");
  command += ' ';
  append_shell_quoted_arg(command, "sh");
  command += ' ';
  append_shell_quoted_arg(command, "-c");
  command += ' ';
  append_shell_quoted_arg(command, REMOTE_TRANSACTION);
  command += ' ';
  append_shell_quoted_arg(command, "shit");
  command += ' ';
  append_shell_quoted_arg(command, upload_name);
  command += ' ';
  append_shell_quoted_arg(command, transaction_id);
  if (recovery_only) {
    command += ' ';
    append_shell_quoted_arg(command, "recover");
  }

  return command;
}

fn cleanup_remote_transaction(const Path &ssh, StringView target,
                              StringView upload_name, StringView transaction_id,
                              Allocator allocator) throws -> i32
{
  let arguments = ArrayList<String>{allocator};
  arguments.push(String{"ssh"});
  arguments.push(String{"--"});
  arguments.push(String{target});
  arguments.push(remote_command(upload_name, transaction_id, true, allocator));
  return run_program(ssh, steal(arguments));
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
  scp_arguments.push(String{"-p"});
  scp_arguments.push(String{"--"});
  scp_arguments.push(String{*executable});
  scp_arguments.push(String{destination.view()});
  let status = run_program(*scp, steal(scp_arguments));
  if (status != 0) {
    let const cleanup_status = cleanup_remote_transaction(
        *ssh, arguments[1].view(), upload_name.view(), transaction_id.view(),
        allocator);
    if (cleanup_status != 0) {
      report_soft_builtin_error(ec, cxt, "Remote transaction cleanup failed");
    }
    return status;
  }

  let ssh_arguments = ArrayList<String>{allocator};
  ssh_arguments.push(String{"ssh"});
  ssh_arguments.push(String{"--"});
  ssh_arguments.push(arguments[1].clone());
  ssh_arguments.push(remote_command(upload_name.view(), transaction_id.view(),
                                    false, allocator));

  status = run_program(*ssh, steal(ssh_arguments));
  if (status != 0) {
    let const cleanup_status = cleanup_remote_transaction(
        *ssh, arguments[1].view(), upload_name.view(), transaction_id.view(),
        allocator);
    if (cleanup_status != 0) {
      report_soft_builtin_error(ec, cxt, "Remote transaction cleanup failed");
    }
  }

  return status;
}

} /* namespace shit */
