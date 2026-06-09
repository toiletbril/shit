#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"

#if SHIT_PLATFORM_IS POSIX
#include <sys/resource.h>
#endif

/* ulimit reads or sets a resource limit of the shell, choosing the resource by
   flag. Only the operating system enforces these, so a platform without
   resource limits reports them as unlimited and ignores a set. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-f|-n|-t|-u] [limit]");

FLAG(FILE_SIZE, Bool, 'f', "",
     "The largest file the shell may create, in "
     "512-byte blocks.");
FLAG(OPEN_FILES, Bool, 'n', "", "The largest number of open file descriptors.");
FLAG(CPU_TIME, Bool, 't', "", "The most CPU time in seconds.");
FLAG(PROCESSES, Bool, 'u', "", "The most processes for the user.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

#if SHIT_PLATFORM_IS POSIX

namespace {

/* The resource selected by the flags, with the divisor ulimit reports it in. */
struct resource
{
  int which;
  rlim_t units_per_value;
};

cold resource selected_resource() throws
{
  if (FLAG_OPEN_FILES.is_enabled()) return {RLIMIT_NOFILE, 1};
  if (FLAG_CPU_TIME.is_enabled()) return {RLIMIT_CPU, 1};
  if (FLAG_PROCESSES.is_enabled()) return {RLIMIT_NPROC, 1};
  /* The file-size limit is the default, reported in 512-byte blocks. */
  return {RLIMIT_FSIZE, 512};
}

} /* namespace */

cold i32 Ulimit::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  unused(cxt);

  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  ASSERT(!args.is_empty());

  let const resource = selected_resource();

  struct rlimit limit{};
  if (getrlimit(resource.which, &limit) != 0)
    throw Error{"could not read the limit: " +
                os::last_system_error_message()};

  /* A bare flag reads the soft limit, an operand sets both the soft and the
     hard limit. */
  if (args.count() < 2) {
    String out{};
    if (limit.rlim_cur == RLIM_INFINITY)
      out = String{"unlimited"};
    else
      out = utils::uint_to_text(limit.rlim_cur / resource.units_per_value);
    ec.print_to_stdout(out + "\n");
    return 0;
  }

  let const &requested = args[1];
  if (requested == "unlimited") {
    limit.rlim_cur = RLIM_INFINITY;
    limit.rlim_max = RLIM_INFINITY;
  } else {
    let const value =
        static_cast<rlim_t>(std::strtoull(requested.c_str(), nullptr, 10)) *
        resource.units_per_value;
    limit.rlim_cur = value;
    limit.rlim_max = value;
  }

  if (setrlimit(resource.which, &limit) != 0)
    throw Error{"could not set the limit: " +
                os::last_system_error_message()};

  return 0;
}

#else /* not POSIX */

i32 Ulimit::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  unused(cxt);

  let const args = PARSE_BUILTIN_ARGS(ec);
  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* This platform does not expose resource limits, so report unlimited. */
  if (args.count() < 2) ec.print_to_stdout("unlimited\n");
  return 0;
}

#endif

Ulimit::Ulimit() = default;

pure Builtin::Kind Ulimit::kind() const wontthrow { return Kind::Ulimit; }

} /* namespace shit */
