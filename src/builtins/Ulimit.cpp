#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

#if SHIT_PLATFORM_IS POSIX
#include <sys/resource.h>
#endif

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-HSacdflmnpstuvw] [limit]");

HELP_DESCRIPTION_DECL(
    "The ulimit builtin reads or sets a resource limit of the shell, choosing "
    "the resource by flag and defaulting to the file size. A bare flag prints "
    "the limit, an operand sets it, and -a prints every limit. On a platform "
    "without resource limits every limit reads as unlimited and a set is "
    "ignored.");

FLAG(CPU_TIME, Bool, 't', "", "The most CPU time in seconds.");
FLAG(FILE_SIZE, Bool, 'f', "",
     "The largest file the shell may create, in 512-byte blocks.");
FLAG(DATA_SIZE, Bool, 'd', "", "The data segment size in kbytes.");
FLAG(STACK_SIZE, Bool, 's', "", "The stack size in kbytes.");
FLAG(CORE_SIZE, Bool, 'c', "", "The core dump size in 512-byte blocks.");
FLAG(RSS_SIZE, Bool, 'm', "", "The resident set size in kbytes.");
FLAG(LOCKED_MEMORY, Bool, 'l', "", "The locked-in-memory size in kbytes.");
FLAG(PROCESSES_P, Bool, 'p', "", "The most processes for the user.");
FLAG(OPEN_FILES, Bool, 'n', "", "The largest number of open file descriptors.");
FLAG(VIRTUAL_MEMORY, Bool, 'v', "", "The virtual memory size in kbytes.");
FLAG(FILE_LOCKS, Bool, 'w', "", "The most file locks.");
FLAG(PROCESSES, Bool, 'u', "", "The most processes for the user.");
FLAG(ALL, Bool, 'a', "", "Report every limit.");
FLAG(HARD, Bool, 'H', "", "Read or set the hard limit.");
FLAG(SOFT, Bool, 'S', "", "Read or set the soft limit.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Ulimit);

namespace shit {

#if SHIT_PLATFORM_IS POSIX

namespace {

/* The order matches dash so -a prints the same table. */
struct resource_entry
{
  const char *label;
  int which;
  rlim_t units_per_value;
};

constexpr resource_entry RESOURCE_TABLE[] = {
    {"time(seconds)",         RLIMIT_CPU,     1   },
    {"file(blocks)",          RLIMIT_FSIZE,   512 },
    {"data(kbytes)",          RLIMIT_DATA,    1024},
    {"stack(kbytes)",         RLIMIT_STACK,   1024},
    {"coredump(blocks)",      RLIMIT_CORE,    512 },
#ifdef RLIMIT_RSS
    {"memory(kbytes)",        RLIMIT_RSS,     1024},
#endif
#ifdef RLIMIT_MEMLOCK
    {"locked memory(kbytes)", RLIMIT_MEMLOCK, 1024},
#endif
#ifdef RLIMIT_NPROC
    {"process",               RLIMIT_NPROC,   1   },
#endif
    {"nofiles",               RLIMIT_NOFILE,  1   },
#ifdef RLIMIT_AS
    {"vmemory(kbytes)",       RLIMIT_AS,      1024},
#endif
#ifdef RLIMIT_LOCKS
    {"locks",                 RLIMIT_LOCKS,   1   },
#endif
#ifdef RLIMIT_RTPRIO
    {"rtprio",                RLIMIT_RTPRIO,  1   },
#endif
};

fn selected_resource() throws -> resource_entry
{
  if (FLAG_CPU_TIME.is_enabled()) return {"time(seconds)", RLIMIT_CPU, 1};
  if (FLAG_DATA_SIZE.is_enabled()) return {"data(kbytes)", RLIMIT_DATA, 1024};
  if (FLAG_STACK_SIZE.is_enabled())
    return {"stack(kbytes)", RLIMIT_STACK, 1024};
  if (FLAG_CORE_SIZE.is_enabled())
    return {"coredump(blocks)", RLIMIT_CORE, 512};
  if (FLAG_OPEN_FILES.is_enabled()) return {"nofiles", RLIMIT_NOFILE, 1};
#ifdef RLIMIT_RSS
  if (FLAG_RSS_SIZE.is_enabled()) return {"memory(kbytes)", RLIMIT_RSS, 1024};
#endif
#ifdef RLIMIT_MEMLOCK
  if (FLAG_LOCKED_MEMORY.is_enabled())
    return {"locked memory(kbytes)", RLIMIT_MEMLOCK, 1024};
#endif
#ifdef RLIMIT_NPROC
  if (FLAG_PROCESSES.is_enabled() || FLAG_PROCESSES_P.is_enabled())
    return {"process", RLIMIT_NPROC, 1};
#endif
#ifdef RLIMIT_AS
  if (FLAG_VIRTUAL_MEMORY.is_enabled())
    return {"vmemory(kbytes)", RLIMIT_AS, 1024};
#endif
#ifdef RLIMIT_LOCKS
  if (FLAG_FILE_LOCKS.is_enabled()) return {"locks", RLIMIT_LOCKS, 1};
#endif
  return {"file(blocks)", RLIMIT_FSIZE, 512};
}

fn render_limit(const struct rlimit &limit, rlim_t divisor,
                Allocator allocator) throws -> String
{
  const rlim_t value = FLAG_HARD.is_enabled() ? limit.rlim_max : limit.rlim_cur;
  if (value == RLIM_INFINITY) return String{allocator, "unlimited"};
  return String::from(value / divisor, allocator);
}

} // namespace

cold fn Ulimit::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  ASSERT(!args.is_empty());

  if (FLAG_ALL.is_enabled()) {
    let out = String{cxt.scratch_allocator()};
    for (let const &entry : RESOURCE_TABLE) {
      struct rlimit limit{};
      if (getrlimit(entry.which, &limit) != 0) continue;
      let const label = String{cxt.scratch_allocator(), entry.label};
      out += label;
      for (usize pad = label.count(); pad < 20; pad++)
        out.push(' ');
      out.push(' ');
      out +=
          render_limit(limit, entry.units_per_value, cxt.scratch_allocator());
      out.push('\n');
    }
    ec.print_to_stdout(out);
    return 0;
  }

  let const resource = selected_resource();

  struct rlimit limit{};
  if (getrlimit(resource.which, &limit) != 0)
    throw Error{"Unable to read the resource limit because " +
                os::last_system_error_message()};

  if (args.count() < 2) {
    LOG(Debug, "ulimit reading the '%s' limit", resource.label);
    ec.print_to_stdout(
        render_limit(limit, resource.units_per_value, cxt.scratch_allocator()) +
        "\n");
    return 0;
  }

  LOG(Debug, "ulimit changing the '%s' limit to '%s'", resource.label,
      args[1].c_str());

  let const &requested = args[1];
  rlim_t value = RLIM_INFINITY;
  if (requested != "unlimited") {
    value = static_cast<rlim_t>(std::strtoull(requested.c_str(), nullptr, 10)) *
            resource.units_per_value;
  }

  /* Naming neither -H nor -S, or both together, sets both, the way dash does.
   */
  if (FLAG_HARD.is_enabled() || !FLAG_SOFT.is_enabled()) limit.rlim_max = value;
  if (FLAG_SOFT.is_enabled() || !FLAG_HARD.is_enabled()) limit.rlim_cur = value;

  if (setrlimit(resource.which, &limit) != 0)
    throw Error{"Unable to set the resource limit because " +
                os::last_system_error_message()};

  return 0;
}

#else /* not POSIX */

fn Ulimit::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  unused(cxt);

  let const args = PARSE_BUILTIN_ARGS(ec);
  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  if (args.count() < 2) ec.print_to_stdout("unlimited\n");
  return 0;
}

#endif

Ulimit::Ulimit() = default;

pure fn Ulimit::kind() const wontthrow -> Builtin::Kind { return Kind::Ulimit; }

} // namespace shit
