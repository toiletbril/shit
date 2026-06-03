#include "Builtin.hpp"

#include "Debug.hpp"
#include "Errors.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Utils.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace shit {

void
show_builtin_help_impl(const ExecContext &ec,
                       const std::vector<std::string> &hs,
                       const std::vector<Flag *> &fl)
{
  std::string h{};
  h += make_synopsis(ec.args()[0], hs);
  h += '\n';
  h += make_flag_help(fl);
  h += '\n';
  ec.print_to_stdout(h);
}

std::optional<Builtin::Kind>
search_builtin(std::string_view builtin_name)
{
  if (auto b = BUILTINS.find(std::string{builtin_name}); b != BUILTINS.end())
    return b->second;

  return std::nullopt;
}

i32
execute_builtin(ExecContext &&ec, EvalContext &cxt)
{
  std::unique_ptr<Builtin> b{};

  switch (ec.builtin_kind()) {
    BUILTIN_SWITCH_CASES();
  default:
    SHIT_UNREACHABLE("Unhandled builtin of kind %d",
                     SHIT_ENUM(ec.builtin_kind()));
  }

  /* A builtin runs inside the shell process, so it keeps the shell's own signal
     handlers. Resetting them to the default here would let a Ctrl-C during a
     builtin terminate the whole shell, and it cost two extra syscalls on every
     builtin command. */
  SHIT_DEFER { ec.close_fds(); };

  try {
    return b->execute(ec, cxt);
  } catch (const Error &e) {
    throw ErrorWithLocation{ec.source_location(),
                            "Builtin '" + ec.program() + "': " + e.message()};
  }
}

Builtin::Builtin() = default;

} /* namespace shit */
