#pragma once

#include "Builtin.hpp"
#include "Common.hpp"
#include "Path.hpp"

namespace shit {

class ResolvedCommand
{
public:
  enum class Kind : u8
  {
    Builtin,
    Program,
    /* A command that did not resolve to a builtin or a program, built only for
       a pipeline stage whose command was not found, so the stage yields 127
       while the rest of the pipeline still runs. The single-command path throws
       CommandNotFound instead. */
    Unresolved,
  };

  Kind kind{Kind::Program};
  Builtin::Kind builtin_kind{};
  Path program_path{};

  mustuse static ResolvedCommand from_builtin(Builtin::Kind chosen_builtin)
  {
    ResolvedCommand resolved{};
    resolved.kind = Kind::Builtin;
    resolved.builtin_kind = chosen_builtin;
    return resolved;
  }

  mustuse static ResolvedCommand from_program(Path path)
  {
    ResolvedCommand resolved{};
    resolved.kind = Kind::Program;
    resolved.program_path = steal(path);
    return resolved;
  }

  mustuse static ResolvedCommand from_unresolved()
  {
    ResolvedCommand resolved{};
    resolved.kind = Kind::Unresolved;
    return resolved;
  }

  mustuse bool is_builtin() const { return kind == Kind::Builtin; }
  mustuse bool is_unresolved() const { return kind == Kind::Unresolved; }
};

} // namespace shit
