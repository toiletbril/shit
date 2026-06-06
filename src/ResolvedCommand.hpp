#pragma once

#include "Builtin.hpp"
#include "Common.hpp"
#include "Path.hpp"

#include <utility>

namespace shit {

/* A command name resolved to what runs it, either a builtin chosen by its Kind
   or an external program at a Path. This is the specific two-case struct that
   replaces a std::variant of the two, so the discriminant and the payload read
   plainly and carry no library machinery. The unused payload of the inactive
   case stays default-constructed, which is cheap for both a Kind and an empty
   Path. */
class ResolvedCommand
{
public:
  enum class Kind : u8
  {
    Builtin,
    Program,
  };

  Kind kind{Kind::Program};
  Builtin::Kind builtin_kind{};
  Path program_path{};

  [[nodiscard]] static ResolvedCommand
  from_builtin(Builtin::Kind chosen_builtin)
  {
    ResolvedCommand resolved{};
    resolved.kind = Kind::Builtin;
    resolved.builtin_kind = chosen_builtin;
    return resolved;
  }

  [[nodiscard]] static ResolvedCommand from_program(Path path)
  {
    ResolvedCommand resolved{};
    resolved.kind = Kind::Program;
    resolved.program_path = std::move(path);
    return resolved;
  }

  [[nodiscard]] bool is_builtin() const { return kind == Kind::Builtin; }
};

} /* namespace shit */
