#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Shitbox.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-l] [-signal] %job|pid [...]");
HELP_DESCRIPTION_DECL("The kill builtin sends a signal to a job or process.");

FLAG(KILL_LIST, Bool, 'l', "list", "List the signal names and exit.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Kill);

namespace shit {

Kill::Kill() = default;

pure fn Kill::kind() const wontthrow -> Builtin::Kind { return Kind::Kill; }

fn Kill::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* Checked before the signal parsing so -l is not read as a signal named l. */
  if (args.count() > 1 && (args[1] == "-l" || args[1] == "--list")) {
    if (args.count() == 2) {
      ec.print_to_stdout(shitbox::format_signal_list());
      return 0;
    }

    let listing = String{cxt.scratch_allocator()};
    i32 status = 0;

    for (usize i = 2; i < args.count(); i++) {
      let const &spec = args[i];

      if (StringView{spec}.is_all_decimal_digits()) {
        let const parsed = StringView{spec}.to<i64>();
        Maybe<String> name = shit::None;
        if (!parsed.is_error()) {
          let const number = static_cast<i32>(parsed.value());
          name = os::signal_name_from_number(number);
          if (!name.has_value() && number > 128) {
            name = os::signal_name_from_number(number - 128);
          }
        }

        if (name.has_value()) {
          listing += *name;
          listing += '\n';
        } else {
          report_soft_builtin_error(
              ec, cxt, StringView{"'"} + spec + "' is not a valid signal");
          status = 1;
        }
      } else if (let const number = os::signal_number_from_name(spec);
                 number.has_value())
      {
        listing += String::from(*number, cxt.scratch_allocator()).view();
        listing += '\n';
      } else {
        report_soft_builtin_error(
            ec, cxt, StringView{"'"} + spec + "' is not a valid signal");
        status = 1;
      }
    }

    ec.print_to_stdout(listing);
    return status;
  }

  usize first_target = 1;
  let signal_number = os::signal_number_from_name("TERM").value_or(15);

  if (args.count() > 1 && args[1] == "--") {
    first_target = 2;
  } else if (args.count() > 1 && (args[1] == "-s" || args[1] == "-n")) {
    if (args.count() < 3) return report_usage_error(ec, cxt, ec.program());

    let const spec = String{cxt.scratch_allocator(), args[2]};
    if (let const parsed = spec.view().to<i64>();
        !parsed.is_error() && spec[0] >= '0' && spec[0] <= '9')
    {
      signal_number = static_cast<i32>(parsed.value());
    } else if (let const resolved = os::signal_number_from_name(spec);
               resolved.has_value())
    {
      signal_number = *resolved;
    } else {
      throw Error{"'" + spec + "' is not a valid signal"};
    }

    first_target = 3;
  } else if (args.count() > 1 && args[1].length() > 1 && args[1][0] == '-') {
    let const name = String{cxt.scratch_allocator(), args[1].substring(1)};
    if (let const parsed_signal = name.view().to<i64>();
        !parsed_signal.is_error() && name[0] >= '0' && name[0] <= '9')
    {
      /* The all-digits guard rejects a doubled minus such as --9, which would
         otherwise parse as the negative signal -9 and reach kill with an
         invalid number. */
      signal_number = static_cast<i32>(parsed_signal.value());
    } else if (let const resolved = os::signal_number_from_name(name);
               resolved.has_value())
    {
      LOG(Debug, "kill resolved signal '%s' to number %d", name.c_str(),
          *resolved);
      signal_number = *resolved;
    } else {
      throw Error{"'" + name + "' is not a valid signal"};
    }
    first_target = 2;
  }

  if (first_target >= args.count())
    return report_usage_error(ec, cxt, ec.program());

  i32 status = 0;
  for (usize i = first_target; i < args.count(); i++) {
    const String &target = args[i];

    let pid = os::process{};
    if (!target.is_empty() && target[0] == '%') {
      const ErrorOr<i64> parsed_value =
          StringView{target}.substring(1).to<i64>();
      if (parsed_value.is_error()) {
        report_soft_builtin_error(ec, cxt,
                                  StringView{"'"} + target +
                                      "' is not a valid job or process id");
        status = 1;
        continue;
      }
      job *const job = cxt.find_job(static_cast<int>(parsed_value.value()));
      if (job == nullptr) {
        report_soft_builtin_error(
            ec, cxt, StringView{"'"} + target + "' is not a known job");
        status = 1;
        continue;
      }
      ASSERT(job != nullptr);
      pid = job->pid;
    } else {
      const ErrorOr<i64> parsed_value = target.to<i64>();
      if (parsed_value.is_error()) {
        /* A non-numeric target must not fall through to kill(0), which would
           signal the whole process group including this shell. */
        report_soft_builtin_error(ec, cxt,
                                  StringView{"'"} + target +
                                      "' is not a valid job or process id");
        status = 1;
        continue;
      }
      pid = os::process_from_pid(parsed_value.value());
    }

    LOG(Debug, "kill sending signal %d to target '%s'", signal_number,
        target.c_str());
    if (!os::signal_process(pid, signal_number)) {
      report_soft_builtin_error(ec, cxt,
                                StringView{"Cannot signal '"} + target +
                                    "' because " +
                                    os::last_system_error_message());
      status = 1;
    }
  }

  return status;
}

} // namespace shit
