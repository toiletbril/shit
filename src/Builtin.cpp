#include "Builtin.hpp"

#include "Debug.hpp"
#include "Errors.hpp"
#include "Os.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Utils.hpp"

#include <optional>
#include <string>
#include <unordered_map>

namespace shit {

static const std::unordered_map<std::string, Builtin::Kind> BUILTINS = {
    {"echo", Builtin::Kind::Echo},
    {"exit", Builtin::Kind::Exit},
    {"cd",   Builtin::Kind::Cd  },
};

std::optional<Builtin::Kind>
search_builtin(std::string_view builtin_name)
{
  std::string lower_builtin_name = utils::lowercase_string(builtin_name);

  if (auto b = BUILTINS.find(lower_builtin_name.c_str()); b != BUILTINS.end()) {
    return b->second;
  }

  return std::nullopt;
}

i32
execute_builtin(utils::ExecContext &&ec)
{
  std::unique_ptr<Builtin> b;

  switch (ec.builtin_kind()) {
    /* clang-format off */
  case Builtin::Kind::Echo: b.reset(new Echo); break;
  case Builtin::Kind::Cd:   b.reset(new Cd); break;
  case Builtin::Kind::Exit: b.reset(new Exit); break;
    /* clang-format on */

  default:
    SHIT_UNREACHABLE("Unhandled builtin of kind %d", E(ec.builtin_kind()));
  }

  SHIT_DEFER { ec.close_fds(); };

  try {
    i32 ret = b->execute(ec);
    return ret;
  } catch (Error &err) {
    throw ErrorWithLocation{ec.source_location(), "Builtin \"" + ec.args()[0] +
                                                      "\": " + err.message()};
  }
}

/**
 * class: Builtin
 */
Builtin::Builtin() = default;

} /* namespace shit */
