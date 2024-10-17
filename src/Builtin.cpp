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
show_builtin_help_impl(const ExecContext              &ec,
                       const std::vector<std::string> &hs,
                       const std::vector<Flag *>      &fl)
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
  if (auto b = BUILTINS.find(builtin_name.data()); b != BUILTINS.end())
    return b->second;

  return std::nullopt;
}

i32
execute_builtin(ExecContext &&ec)
{
  std::unique_ptr<Builtin> b{};

  switch (ec.builtin_kind()) {
    BUILTIN_SWITCH_CASES();
  default:
    SHIT_UNREACHABLE("Unhandled builtin of kind %d",
                     SHIT_ENUM(ec.builtin_kind()));
  }

  os::reset_signal_handlers();

  /* TODO: Figure signals for builtins. */
  SHIT_DEFER
  {
    ec.close_fds();
    os::set_default_signal_handlers();
  };

  try {
    return b->execute(ec);
  } catch (const Error &e) {
    throw ErrorWithLocation{ec.source_location(),
                            "Builtin '" + ec.program() + "': " + e.message()};
  }
}

Builtin::Builtin() = default;

} /* namespace shit */
