#pragma once

#include "Cli.hpp"
#include "Common.hpp"
#include "Maybe.hpp"
#include "Platform.hpp"

#include <optional>

/* TODO: test */

namespace shit {

class ExecContext;
class EvalContext;

class Builtin
{
public:
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
    Exec,
    Type,
    CommandBuiltin,
    Readonly,
    Local,
    Times,
    Ulimit,
    Hash,
    Alias,
    Unalias,
    Jobs,
    Fg,
    Bg,
    Wait,
    Kill,
    Time,
    Bench,
    Newgrp,
  };

  void set_fds(os::descriptor in, os::descriptor out) throws;
  void print_to_stdout(StringView s) const throws;

  pure virtual Kind kind() const wontthrow = 0;
  virtual i32 execute(ExecContext &ec, EvalContext &cxt) const throws = 0;

  virtual ~Builtin() = default;

protected:
  Builtin();
};

inline constexpr StaticStringMap<Builtin::Kind>::entry BUILTIN_ENTRIES[] = {
    {PackedStringKey::from_literal("echo"),     Builtin::Kind::Echo          },
    {PackedStringKey::from_literal("exit"),     Builtin::Kind::Exit          },
    {PackedStringKey::from_literal("cd"),       Builtin::Kind::Cd            },
    {PackedStringKey::from_literal("pwd"),      Builtin::Kind::Pwd           },
    {PackedStringKey::from_literal("which"),    Builtin::Kind::Which         },
    {PackedStringKey::from_literal("whoami"),   Builtin::Kind::WhoAmI        },
    {PackedStringKey::from_literal("export"),   Builtin::Kind::Export        },
    {PackedStringKey::from_literal("break"),    Builtin::Kind::Break         },
    {PackedStringKey::from_literal("continue"), Builtin::Kind::Continue      },
    {PackedStringKey::from_literal("return"),   Builtin::Kind::Return        },
    {PackedStringKey::from_literal(":"),        Builtin::Kind::True          },
    {PackedStringKey::from_literal("true"),     Builtin::Kind::True          },
    {PackedStringKey::from_literal("false"),    Builtin::Kind::False         },
    {PackedStringKey::from_literal("test"),     Builtin::Kind::Test          },
    {PackedStringKey::from_literal("["),        Builtin::Kind::Test          },
    {PackedStringKey::from_literal("."),        Builtin::Kind::Source        },
    {PackedStringKey::from_literal("source"),   Builtin::Kind::Source        },
    {PackedStringKey::from_literal("eval"),     Builtin::Kind::Eval          },
    {PackedStringKey::from_literal("set"),      Builtin::Kind::Set           },
    {PackedStringKey::from_literal("shift"),    Builtin::Kind::Shift         },
    {PackedStringKey::from_literal("unset"),    Builtin::Kind::Unset         },
    {PackedStringKey::from_literal("read"),     Builtin::Kind::Read          },
    {PackedStringKey::from_literal("printf"),   Builtin::Kind::Printf        },
    {PackedStringKey::from_literal("umask"),    Builtin::Kind::Umask         },
    {PackedStringKey::from_literal("getopts"),  Builtin::Kind::Getopts       },
    {PackedStringKey::from_literal("trap"),     Builtin::Kind::Trap          },
    {PackedStringKey::from_literal("exec"),     Builtin::Kind::Exec          },
    {PackedStringKey::from_literal("type"),     Builtin::Kind::Type          },
    {PackedStringKey::from_literal("command"),  Builtin::Kind::CommandBuiltin},
    {PackedStringKey::from_literal("readonly"), Builtin::Kind::Readonly      },
    {PackedStringKey::from_literal("local"),    Builtin::Kind::Local         },
    {PackedStringKey::from_literal("times"),    Builtin::Kind::Times         },
    {PackedStringKey::from_literal("ulimit"),   Builtin::Kind::Ulimit        },
    {PackedStringKey::from_literal("hash"),     Builtin::Kind::Hash          },
    {PackedStringKey::from_literal("alias"),    Builtin::Kind::Alias         },
    {PackedStringKey::from_literal("unalias"),  Builtin::Kind::Unalias       },
    {PackedStringKey::from_literal("jobs"),     Builtin::Kind::Jobs          },
    {PackedStringKey::from_literal("fg"),       Builtin::Kind::Fg            },
    {PackedStringKey::from_literal("bg"),       Builtin::Kind::Bg            },
    {PackedStringKey::from_literal("wait"),     Builtin::Kind::Wait          },
    {PackedStringKey::from_literal("kill"),     Builtin::Kind::Kill          },
    {PackedStringKey::from_literal("time"),     Builtin::Kind::Time          },
    {PackedStringKey::from_literal("bench"),    Builtin::Kind::Bench         },
    {PackedStringKey::from_literal("newgrp"),   Builtin::Kind::Newgrp        },
};

inline constexpr StaticStringMap<Builtin::Kind> BUILTINS{
    BUILTIN_ENTRIES, sizeof(BUILTIN_ENTRIES) / sizeof(BUILTIN_ENTRIES[0])};

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
  B_CASE(Trap);                                                                \
  B_CASE(Exec);                                                                \
  B_CASE(Type);                                                                \
  B_CASE(CommandBuiltin);                                                      \
  B_CASE(Readonly);                                                            \
  B_CASE(Local);                                                               \
  B_CASE(Times);                                                               \
  B_CASE(Ulimit);                                                              \
  B_CASE(Hash);                                                                \
  B_CASE(Alias);                                                               \
  B_CASE(Unalias);                                                             \
  B_CASE(Jobs);                                                                \
  B_CASE(Fg);                                                                  \
  B_CASE(Bg);                                                                  \
  B_CASE(Wait);                                                                \
  B_CASE(Kill);                                                                \
  B_CASE(Time);                                                                \
  B_CASE(Bench);                                                               \
  B_CASE(Newgrp)

#define BUILTIN_STRUCT(b)                                                      \
  class b : public Builtin                                                     \
  {                                                                            \
  public:                                                                      \
    b();                                                                       \
                                                                               \
    pure Kind kind() const wontthrow override;                                 \
    i32 execute(ExecContext &ec, EvalContext &cxt) const throws override;      \
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
BUILTIN_STRUCT(Exec);
BUILTIN_STRUCT(Type);
BUILTIN_STRUCT(CommandBuiltin);
BUILTIN_STRUCT(Readonly);
BUILTIN_STRUCT(Local);
BUILTIN_STRUCT(Times);
BUILTIN_STRUCT(Ulimit);
BUILTIN_STRUCT(Hash);
BUILTIN_STRUCT(Alias);
BUILTIN_STRUCT(Unalias);
BUILTIN_STRUCT(Jobs);
BUILTIN_STRUCT(Fg);
BUILTIN_STRUCT(Bg);
BUILTIN_STRUCT(Wait);
BUILTIN_STRUCT(Kill);
BUILTIN_STRUCT(Time);
BUILTIN_STRUCT(Bench);
BUILTIN_STRUCT(Newgrp);

class Exit : public Builtin
{
public:
  Exit();

  pure Kind kind() const wontthrow override;
  i32 execute(ExecContext &ec, EvalContext &cxt) const throws override;
};

Maybe<Builtin::Kind> search_builtin(StringView builtin_name) throws;

/* True when the name is one of the POSIX special builtins, the set whose prefix
   assignments persist after the command and whose errors abort a non-interactive
   shell. The test is by name rather than by kind, since : is special while true
   is not though both resolve to one kind. */
fn is_special_builtin_name(StringView name) wontthrow -> bool;

/* The builtin command names, recovered once from BUILTIN_ENTRIES and cached,
   so command completion offers exactly the registered builtins. */
const ArrayList<String> &builtin_names() throws;

void show_builtin_help_impl(const ExecContext &ec,
                            const ArrayList<StringView> &hs,
                            const ArrayList<Flag *> &fl) throws;

#define SHOW_BUILTIN_HELP_AND_RETURN(ec)                                       \
  do {                                                                         \
    show_builtin_help_impl(ec, HELP_SYNOPSIS, FLAG_LIST);                      \
    return 0;                                                                  \
  } while (false)

/* TODO: More granular error location for flags? */
#define PARSE_BUILTIN_ARGS(ec)                                                 \
  parse_flags_vec(FLAG_LIST, ec.args());                                       \
  defer { reset_flags(FLAG_LIST); }

i32 execute_builtin(ExecContext &&ec, EvalContext &cxt) throws;

} /* namespace shit */
