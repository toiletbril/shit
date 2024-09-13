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

std::optional<Builtin::Kind>
search_builtin(std::string_view builtin_name)
{
  std::string lower_builtin_name = utils::lowercase_string(builtin_name);

  if (auto b = BUILTINS.find(lower_builtin_name.c_str()); b != BUILTINS.end())
    return b->second;

  return std::nullopt;
}

i32
execute_builtin(ExecContext &&ec)
{
  std::unique_ptr<Builtin> b;

  switch (ec.builtin_kind()) {
    BUILTIN_SWITCH_CASES();
  default:
    SHIT_UNREACHABLE("Unhandled builtin of kind %d",
                     SHIT_ENUM(ec.builtin_kind()));
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
