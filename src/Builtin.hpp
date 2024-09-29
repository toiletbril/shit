#pragma once

#include "Cli.hpp"
#include "Common.hpp"
#include "Os.hpp"

#include <optional>
#include <string>
#include <unordered_map>

namespace shit {

struct ExecContext;

struct Builtin
{
  enum class Kind : uint8_t
  {
    Echo,
    Cd,
    Exit,
    Pwd,
    Which,
    WhoAmI,
  };

  void set_fds(os::descriptor in, os::descriptor out);
  void print_to_stdout(const std::string &s) const;

  virtual Kind kind() const = 0;
  virtual i32  execute(ExecContext &ec) const = 0;

  virtual ~Builtin() = default;

protected:
  Builtin();
};

const std::unordered_map<std::string, Builtin::Kind> BUILTINS = {
    {"echo",   Builtin::Kind::Echo  },
    {"exit",   Builtin::Kind::Exit  },
    {"cd",     Builtin::Kind::Cd    },
    {"pwd",    Builtin::Kind::Pwd   },
    {"which",  Builtin::Kind::Which },
    {"whoami", Builtin::Kind::WhoAmI},
};

#define B_CASE(btin)                                                           \
  case Builtin::Kind::btin: b.reset(new btin); break

#define BUILTIN_SWITCH_CASES()                                                 \
  B_CASE(Echo);                                                                \
  B_CASE(Cd);                                                                  \
  B_CASE(Exit);                                                                \
  B_CASE(Pwd);                                                                 \
  B_CASE(Which);                                                               \
  B_CASE(WhoAmI)

#define BUILTIN_STRUCT(b)                                                      \
  struct b : public Builtin                                                    \
  {                                                                            \
    b();                                                                       \
                                                                               \
    Kind kind() const override;                                                \
    i32  execute(ExecContext &ec) const override;                              \
  };

BUILTIN_STRUCT(Echo);
BUILTIN_STRUCT(Cd);
BUILTIN_STRUCT(Pwd);
BUILTIN_STRUCT(Which);
BUILTIN_STRUCT(WhoAmI);

struct Exit : public Builtin
{
  Exit();

  Kind             kind() const override;
  [[noreturn]] i32 execute(ExecContext &ec) const override;
};

std::optional<Builtin::Kind> search_builtin(std::string_view builtin_name);

void show_builtin_help_impl(std::string_view p, const ExecContext &ec,
                            const std::vector<std::string> &hs,
                            const std::vector<Flag *>      &fl);

#define SHOW_BUILTIN_HELP(p, ec)                                               \
  show_builtin_help_impl(p, ec, HELP_SYNOPSIS, FLAG_LIST)

#define BUILTIN_ARGS(ec)                                                       \
  parse_flags_vec(FLAG_LIST, ec.args());                                       \
  SHIT_DEFER { reset_flags(FLAG_LIST); }

i32 execute_builtin(ExecContext &&ec);

} /* namespace shit */
