#include "Builtin.hpp"

#include "Debug.hpp"
#include "Errors.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Utils.hpp"

#include <optional>

namespace shit {

cold fn show_builtin_help_impl(const ExecContext &ec,
                               const ArrayList<StringView> &hs,
                               const ArrayList<Flag *> &fl) throws -> void
{
  ASSERT(!ec.args().is_empty());

  String help_text{};
  help_text += make_synopsis(ec.args()[0].view(), hs);
  help_text += '\n';
  help_text += make_flag_help(fl);
  help_text += '\n';
  ec.print_to_stdout(help_text);
}

flatten fn search_builtin(StringView builtin_name) throws
    -> Maybe<Builtin::Kind>
{
  return BUILTINS.find(builtin_name);
}

fn builtin_names() throws -> const ArrayList<String> &
{
  static ArrayList<String> names = [] throws {
    ArrayList<String> collected{};
    for (const StaticStringMap<Builtin::Kind>::entry &entry : BUILTIN_ENTRIES)
      collected.push(entry.key.to_string());
    return collected;
  }();
  return names;
}


fn execute_builtin(ExecContext &&ec, EvalContext &cxt) throws -> i32
{
  ASSERT(!ec.args().is_empty());


  std::unique_ptr<Builtin> b{};

  switch (ec.builtin_kind()) {
    BUILTIN_SWITCH_CASES();
  default: unreachable("Unhandled builtin of kind %d", ENUM(ec.builtin_kind()));
  }

  /* A builtin runs inside the shell process, so it keeps the shell's own signal
     handlers. Resetting them to the default here would let a Ctrl-C during a
     builtin terminate the whole shell, and it cost two extra syscalls on every
     builtin command. */
  defer { ec.close_fds(); };

  /* A builtin stage of a pipeline carries the pipe ends in its context. A
     builtin that runs a sub-command, such as eval, command, or the dot source,
     evaluates a fresh command that builds its own context from the shell's real
     descriptors and never sees these pipe ends. The pipe descriptors are placed
     on the real shell fd 0, 1, and 2 for the duration of the builtin so any
     sub-command it spawns inherits them, and the originals are restored after.
     A single builtin that is not a pipeline stage carries no pipe fds, so it
     pays for none of this. */
  const bool has_pipe_descriptors =
      ec.in_fd.has_value() || ec.out_fd.has_value() || ec.err_fd.has_value();

  ArrayList<os::saved_descriptor> saved_descriptors{};
  if (has_pipe_descriptors) {
    if (ec.in_fd)
      saved_descriptors.push(os::save_and_replace_descriptor(0, *ec.in_fd));
    if (ec.out_fd)
      saved_descriptors.push(os::save_and_replace_descriptor(1, *ec.out_fd));
    if (ec.err_fd)
      saved_descriptors.push(os::save_and_replace_descriptor(2, *ec.err_fd));
  }
  defer
  {
    for (usize i = saved_descriptors.count(); i > 0; i--)
      os::restore_descriptor(saved_descriptors[i - 1]);
  };

  ASSERT(b != nullptr);
  try {
    return b->execute(ec, cxt);
  } catch (const Error &e) {
    throw ErrorWithLocation{ec.source_location(), StringView{"Builtin '"} +
                                                      ec.program() +
                                                      "': " + e.message()};
  }
}

Builtin::Builtin() = default;

} /* namespace shit */
