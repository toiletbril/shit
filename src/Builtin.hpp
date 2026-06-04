#pragma once

#include "Cli.hpp"
#include "Common.hpp"
#include "Maybe.hpp"
#include "Os.hpp"

#include <optional>
#include <string>
#include <unordered_map>

/* TODO: test */

namespace shit {

struct ExecContext;
struct EvalContext;

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
    Export,
    Break,
    Continue,
    Return,
    Colon,
    True,
    False,
    Test,
    Source,
    Eval,
    Set,
    Shift,
    Unset,
    Read,
    Printf,
    Umask,
    Getopts,
    Trap,
  };

  void set_fds(os::descriptor in, os::descriptor out);
  void print_to_stdout(const std::string &s) const;

  virtual Kind kind() const = 0;
  virtual i32 execute(ExecContext &ec, EvalContext &cxt) const = 0;

  virtual ~Builtin() = default;

protected:
  Builtin();
};

const std::unordered_map<std::string, Builtin::Kind> BUILTINS = {
    {"echo",     Builtin::Kind::Echo    },
    {"exit",     Builtin::Kind::Exit    },
    {"cd",       Builtin::Kind::Cd      },
    {"pwd",      Builtin::Kind::Pwd     },
    {"which",    Builtin::Kind::Which   },
    {"whoami",   Builtin::Kind::WhoAmI  },
    {"export",   Builtin::Kind::Export  },
    {"break",    Builtin::Kind::Break   },
    {"continue", Builtin::Kind::Continue},
    {"return",   Builtin::Kind::Return  },
    {":",        Builtin::Kind::Colon   },
    {"true",     Builtin::Kind::True    },
    {"false",    Builtin::Kind::False   },
    {"test",     Builtin::Kind::Test    },
    {"[",        Builtin::Kind::Test    },
    {".",        Builtin::Kind::Source  },
    {"source",   Builtin::Kind::Source  },
    {"eval",     Builtin::Kind::Eval    },
    {"set",      Builtin::Kind::Set     },
    {"shift",    Builtin::Kind::Shift   },
    {"unset",    Builtin::Kind::Unset   },
    {"read",     Builtin::Kind::Read    },
    {"printf",   Builtin::Kind::Printf  },
    {"umask",    Builtin::Kind::Umask   },
    {"getopts",  Builtin::Kind::Getopts },
    {"trap",     Builtin::Kind::Trap    },
};

#define B_CASE(btin)                                                           \
  case Builtin::Kind::btin: b.reset(new btin); break

#define BUILTIN_SWITCH_CASES()                                                 \
  B_CASE(Echo);                                                                \
  B_CASE(Cd);                                                                  \
  B_CASE(Exit);                                                                \
  B_CASE(Pwd);                                                                 \
  B_CASE(Which);                                                               \
  B_CASE(WhoAmI);                                                              \
  B_CASE(Export);                                                              \
  B_CASE(Break);                                                               \
  B_CASE(Continue);                                                            \
  B_CASE(Return);                                                              \
  B_CASE(Colon);                                                               \
  B_CASE(True);                                                                \
  B_CASE(False);                                                               \
  B_CASE(Test);                                                                \
  B_CASE(Source);                                                              \
  B_CASE(Eval);                                                                \
  B_CASE(Set);                                                                 \
  B_CASE(Shift);                                                               \
  B_CASE(Unset);                                                               \
  B_CASE(Read);                                                                \
  B_CASE(Printf);                                                              \
  B_CASE(Umask);                                                               \
  B_CASE(Getopts);                                                             \
  B_CASE(Trap)

#define BUILTIN_STRUCT(b)                                                      \
  struct b : public Builtin                                                    \
  {                                                                            \
    b();                                                                       \
                                                                               \
    Kind kind() const override;                                                \
    i32 execute(ExecContext &ec, EvalContext &cxt) const override;             \
  };

BUILTIN_STRUCT(Echo);
BUILTIN_STRUCT(Cd);
BUILTIN_STRUCT(Pwd);
BUILTIN_STRUCT(Which);
BUILTIN_STRUCT(WhoAmI);
BUILTIN_STRUCT(Export);
BUILTIN_STRUCT(Break);
BUILTIN_STRUCT(Continue);
BUILTIN_STRUCT(Return);
BUILTIN_STRUCT(Colon);
BUILTIN_STRUCT(True);
BUILTIN_STRUCT(False);
BUILTIN_STRUCT(Test);
BUILTIN_STRUCT(Source);
BUILTIN_STRUCT(Eval);
BUILTIN_STRUCT(Set);
BUILTIN_STRUCT(Shift);
BUILTIN_STRUCT(Unset);
BUILTIN_STRUCT(Read);
BUILTIN_STRUCT(Printf);
BUILTIN_STRUCT(Umask);
BUILTIN_STRUCT(Getopts);
BUILTIN_STRUCT(Trap);

struct Exit : public Builtin
{
  Exit();

  Kind kind() const override;
  [[noreturn]] i32 execute(ExecContext &ec, EvalContext &cxt) const override;
};

Maybe<Builtin::Kind> search_builtin(std::string_view builtin_name);

void show_builtin_help_impl(const ExecContext &ec,
                            const std::vector<std::string> &hs,
                            const std::vector<Flag *> &fl);

#define SHOW_BUILTIN_HELP_AND_RETURN(ec)                                       \
  do {                                                                         \
    show_builtin_help_impl(ec, HELP_SYNOPSIS, FLAG_LIST);                      \
    return 0;                                                                  \
  } while (false)

/* TODO: More granular error location for flags? */
#define PARSE_BUILTIN_ARGS(ec)                                                 \
  parse_flags_vec(FLAG_LIST, ec.args());                                       \
  SHIT_DEFER { reset_flags(FLAG_LIST); }

i32 execute_builtin(ExecContext &&ec, EvalContext &cxt);

} /* namespace shit */
