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
umask 077
upload=$1
candidate=
target=
lock=
backup=
lock_owned=0
lock_published=0
had_target=0
rollback_active=0
committed=0
cleanup() {
  transaction_status=$?
  [ "$#" -eq 0 ] || transaction_status=$1
  if [ "$rollback_active" -eq 1 ] && [ "$committed" -eq 0 ] &&
     [ "$transaction_status" -eq 0 ]; then
    transaction_status=1
  fi
  cleanup_failed=0
  trap - 0 1 2 15
  if [ "$lock_owned" -eq 1 ]; then
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
    [ -z "$upload" ] || shitbox rm -f "$upload" || cleanup_failed=1
    if [ "$committed" -eq 1 ] || [ "$rollback_active" -eq 0 ]; then
      [ -z "$backup" ] || shitbox rm -f "$backup" || cleanup_failed=1
    fi
    if [ "$cleanup_failed" -eq 0 ]; then
      shitbox rm -f "$lock/upload" "$lock/candidate" \
        "$lock/had-target" "$lock/rollback" "$lock/committed" || cleanup_failed=1
      [ "$lock_published" -eq 0 ] || shitbox rm -f "$lock_path" || cleanup_failed=1
      if [ "$cleanup_failed" -eq 0 ]; then
        shitbox rm -f "$lock/owner" || cleanup_failed=1
        [ "$cleanup_failed" -ne 0 ] || shitbox rmdir "$lock" || cleanup_failed=1
      fi
    fi
  else
    [ -z "$candidate" ] || shitbox rm -f "$candidate" || cleanup_failed=1
    [ -z "$upload" ] || shitbox rm -f "$upload" || cleanup_failed=1
  fi
  [ "$cleanup_failed" -eq 0 ] || exit 1
  exit "$transaction_status"
}
load_stale_lock_record() {
  stale_owner=${lock_record%%:*}
  stale_lock_name=${lock_record#*:}
  [ "$stale_lock_name" != "$lock_record" ] || return 1
  case $stale_owner in ''|*[!0-9]*) return 1 ;; esac
  case $stale_lock_name in .shit-assimilate-*.lock) ;; *) return 1 ;; esac
  stale_transaction_id=${stale_lock_name#.shit-assimilate-}
  stale_transaction_id=${stale_transaction_id%.lock}
  stale_process_id=${stale_transaction_id%%-*}
  stale_timestamp=${stale_transaction_id#*-}
  [ "$stale_timestamp" != "$stale_transaction_id" ] || return 1
  case $stale_process_id in ''|*[!0-9]*) return 1 ;; esac
  case $stale_timestamp in ''|*[!0-9]*) return 1 ;; esac
  [ "$stale_transaction_id" = "$stale_process_id-$stale_timestamp" ] || return 1
  stale_lock=$install_dir/$stale_lock_name
  [ -d "$stale_lock" ] && [ ! -L "$stale_lock" ] || return 1
  stale_upload=.shit-assimilate-$stale_transaction_id.upload
  stale_candidate=$install_dir/.shit-assimilate-$stale_transaction_id.candidate
  [ "${1-0}" -eq 0 ] || return 0
  [ ! -L "$stale_lock/upload" ] || return 1
  [ ! -L "$stale_lock/candidate" ] || return 1
  if [ -e "$stale_lock/upload" ]; then
    recorded_upload=$(shitbox cat "$stale_lock/upload") || return 1
    [ "$recorded_upload" = "$stale_upload" ] || return 1
  fi
  if [ -e "$stale_lock/candidate" ]; then
    recorded_candidate=$(shitbox cat "$stale_lock/candidate") || return 1
    [ "$recorded_candidate" = "$stale_candidate" ] || return 1
  fi
}
recover_stale_lock() {
  stale_backup=$stale_lock/backup
  if [ ! -e "$stale_lock/committed" ] && [ -e "$stale_lock/rollback" ]; then
    if [ -e "$stale_lock/had-target" ]; then
      if [ -e "$stale_backup" ] || [ -L "$stale_backup" ]; then
        shitbox mv -f "$stale_backup" "$target" || return 1
      else
        [ -e "$target" ] || [ -L "$target" ] || return 1
      fi
    else
      shitbox rm -f "$target" || return 1
    fi
  fi
  shitbox rm -f "$stale_candidate" "$stale_upload" "$stale_backup" || return 1
  shitbox rm -f "$stale_lock/upload" "$stale_lock/candidate" \
    "$stale_lock/had-target" "$stale_lock/rollback" \
    "$stale_lock/committed" || return 1
  if [ "${1-0}" -eq 1 ]; then
    current_lock_record=$(shitbox cat "$lock_path") || return 1
    [ "$current_lock_record" = "$lock_record" ] || return 1
    shitbox rm -f "$lock_path" || return 1
  fi
  shitbox rm -f "$stale_lock/owner" || return 1
  shitbox rmdir "$stale_lock" || return 1
}
recover_existing_lock() {
  lock_record=$(shitbox cat "$lock_path") || return 1
  load_stale_lock_record || return 1
  recover_stale_lock 1
}
recover_orphan_locks() {
  for orphan_lock in "$install_dir"/.shit-assimilate-*.lock; do
    [ -d "$orphan_lock" ] || continue
    [ ! -L "$orphan_lock" ] || return 1
    if [ ! -e "$orphan_lock/rollback" ] && [ ! -e "$orphan_lock/committed" ]; then
      lock_record=0:${orphan_lock##*/}
      load_stale_lock_record 1 || return 1
    else
      lock_record=$(shitbox cat "$orphan_lock/owner") || return 1
      load_stale_lock_record || return 1
    fi
    [ "$stale_lock" = "$orphan_lock" ] || return 1
    recover_stale_lock 0 || return 1
  done
}
recover_all_stale_locks() {
  if [ -e "$lock_path" ] || [ -L "$lock_path" ]; then
    recover_existing_lock || return 1
  fi
  recover_orphan_locks
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
[ "${4-}" != recover ] || {
  recover_all_stale_locks
  shitbox rm -f "$upload"
  trap - 0 1 2 15
  exit 0
}
[ ! -d "$target" ] || exit 1
recover_all_stale_locks
trap '' 1 2 15
new_lock=$install_dir/.shit-assimilate-$2.lock
shitbox mkdir "$new_lock"
lock=$new_lock
lock_owned=1
backup=$lock/backup
printf '%s:%s\n' "$$" "${lock##*/}" > "$lock/owner"
printf '%s\n' "$upload" > "$lock/upload"
printf '%s\n' "$candidate" > "$lock/candidate"
if ! shitbox ln -s "$lock/owner" "$lock_path"; then
  recover_existing_lock
  shitbox ln -s "$lock/owner" "$lock_path"
fi
lock_published=1
trap 'exit 129' 1
trap 'exit 130' 2
trap 'exit 143' 15
shitbox mv "$upload" "$candidate"
if ! candidate_identity=$("$candidate" -p --mood sh -c 'shitbox --binary-identity'); then
  cleanup 1
fi
[ -n "$candidate_identity" ] && [ "$candidate_identity" = "$3" ] || cleanup 1
if [ -e "$target" ] || [ -L "$target" ]; then
  had_target=1
  : > "$lock/had-target"
fi
rollback_active=1
: > "$lock/rollback"
[ "$had_target" -eq 0 ] || shitbox mv -f "$target" "$backup"
shitbox mv -f "$candidate" "$target"
candidate=
if ! target_identity=$("$target" -p --mood sh -c 'shitbox --binary-identity'); then
  cleanup 1
fi
[ -n "$target_identity" ] && [ "$target_identity" = "$candidate_identity" ] || cleanup 1
trap '' 1 2 15
if ! : > "$lock/committed"; then
  cleanup 1
  exit 1
fi
committed=1
rollback_active=0
shitbox rm -f "$lock/rollback"
[ "$had_target" -eq 0 ] || shitbox rm -f "$backup"
shitbox rm -f "$lock/upload" "$lock/candidate" \
  "$lock/had-target" "$lock/committed"
shitbox rm -f "$lock_path"
lock_published=0
shitbox rm -f "$lock/owner"
shitbox rmdir "$lock"
lock_owned=0
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
                  StringView expected_identity, bool recovery_only,
                  Allocator allocator) throws -> String
{
  let command = String{allocator};
  command += "exec ";
  let const upload_path = String{allocator, "./"} + upload_name;
  let const do_append_argument = [&](StringView argument) throws {
    append_shell_quoted_arg(command, argument);
    command += ' ';
  };
  do_append_argument(upload_path.view());
  do_append_argument("-p");
  do_append_argument("--mood");
  do_append_argument("sh");
  do_append_argument("-c");
  do_append_argument("shitbox --transaction-lock \"$@\"");
  do_append_argument("shit");
  do_append_argument(".");
  do_append_argument(upload_path.view());
  do_append_argument("-p");
  do_append_argument("--mood");
  do_append_argument("sh");
  do_append_argument("-c");
  do_append_argument(REMOTE_TRANSACTION);
  do_append_argument("shit");
  do_append_argument(upload_name);
  do_append_argument(transaction_id);
  do_append_argument(expected_identity);
  if (recovery_only) do_append_argument("recover");
  command.pop_back();

  return command;
}

fn cleanup_remote_transaction(const Path &ssh, StringView target,
                              StringView upload_name, StringView transaction_id,
                              StringView expected_identity,
                              Allocator allocator) throws -> i32
{
  let arguments = ArrayList<String>{allocator};
  arguments.push(String{"ssh"});
  arguments.push(String{"--"});
  arguments.push(String{target});
  arguments.push(remote_command(upload_name, transaction_id, expected_identity,
                                true, allocator));
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
  bool is_inside_host_literal = false;
  bool has_host_literal = false;
  usize host_literal_start = 0;
  if (arguments.count() == 2) {
    for (usize character_position = 0;
         character_position < arguments[1].count(); character_position++)
    {
      const char character = arguments[1][character_position];
      switch (character) {
      case ' ':
      case '\t':
      case '\r':
      case '\n': has_invalid_target_character = true; break;
      case '[':
        if (has_host_literal || is_inside_host_literal ||
            (character_position != 0 &&
             arguments[1][character_position - 1] != '@'))
        {
          has_invalid_target_character = true;
        }
        has_host_literal = true;
        is_inside_host_literal = true;
        host_literal_start = character_position;
        break;
      case ']':
        if (!is_inside_host_literal ||
            character_position == host_literal_start + 1 ||
            character_position + 1 != arguments[1].count())
        {
          has_invalid_target_character = true;
        }
        is_inside_host_literal = false;
        break;
      case ':':
        if (!is_inside_host_literal) has_invalid_target_character = true;
        break;
      default: break;
      }
    }
  }
  if (is_inside_host_literal) has_invalid_target_character = true;

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
  let const expected_identity =
      utils::file_content_identity(Path{*executable}, allocator);
  if (!expected_identity.has_value()) {
    report_soft_builtin_error(ec, cxt,
                              "Cannot identify this shell's executable");
    return 1;
  }
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
        expected_identity->view(), allocator);
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
                                    expected_identity->view(), false,
                                    allocator));

  status = run_program(*ssh, steal(ssh_arguments));
  if (status != 0) {
    let const cleanup_status = cleanup_remote_transaction(
        *ssh, arguments[1].view(), upload_name.view(), transaction_id.view(),
        expected_identity->view(), allocator);
    if (cleanup_status != 0) {
      report_soft_builtin_error(ec, cxt, "Remote transaction cleanup failed");
    }
  }

  return status;
}

} /* namespace shit */
