#pragma once

#include "Builtin.hpp"
#include "Cli.hpp"
#include "Common.hpp"
#include "Maybe.hpp"
#include "PackedStringKey.hpp"
#include "Platform.hpp"
#include "StaticStringMap.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace shit {

class ExecContext;
class EvalContext;

/* The shitbox subsystem is a busybox-style set of small coreutils hosted by the
   shitbox builtin, so a bare system with only a compiler can run a configure
   and a make under shit. A utility is reached three ways. The shitbox builtin
   runs `shitbox ls`, the bare name ls resolves directly when the toggle is on,
   and a binary renamed to ls acts as the utility through the multicall entry.
 */
namespace shitbox {

class Utility
{
public:
  enum class Kind : uint8_t
  {
    Ls,
    Ln,
    Rm,
    Mkdir,
    Rmdir,
    Cp,
    Mv,
    Cat,
    Tee,
    Touch,
    Basename,
    Dirname,
    Realpath,
    Du,
    Head,
    Tail,
    Wc,
    Seq,
    Tr,
    Grep,
    Sort,
    Uniq,
    Sleep,
    Env,
    Yes,
    Pkill,
    Killall,
    Ps,
    Make,
    Find,
    Which,
    WhoAmI,
    Unlink,
    Calc,
  };

  pure virtual Kind kind() const wontthrow = 0;
  virtual i32 execute(const ExecContext &ec, EvalContext &cxt,
                      const ArrayList<String> &args) const throws = 0;

  virtual ~Utility() = default;

protected:
  Utility();
};

inline constexpr StaticStringMap<Utility::Kind>::entry SHITBOX_ENTRIES[] = {
    {SSK("ls"),       Utility::Kind::Ls      },
    {SSK("ln"),       Utility::Kind::Ln      },
    {SSK("rm"),       Utility::Kind::Rm      },
    {SSK("mkdir"),    Utility::Kind::Mkdir   },
    {SSK("rmdir"),    Utility::Kind::Rmdir   },
    {SSK("cp"),       Utility::Kind::Cp      },
    {SSK("mv"),       Utility::Kind::Mv      },
    {SSK("cat"),      Utility::Kind::Cat     },
    {SSK("tee"),      Utility::Kind::Tee     },
    {SSK("touch"),    Utility::Kind::Touch   },
    {SSK("basename"), Utility::Kind::Basename},
    {SSK("dirname"),  Utility::Kind::Dirname },
    {SSK("realpath"), Utility::Kind::Realpath},
    {SSK("du"),       Utility::Kind::Du      },
    {SSK("head"),     Utility::Kind::Head    },
    {SSK("tail"),     Utility::Kind::Tail    },
    {SSK("wc"),       Utility::Kind::Wc      },
    {SSK("seq"),      Utility::Kind::Seq     },
    {SSK("tr"),       Utility::Kind::Tr      },
    {SSK("grep"),     Utility::Kind::Grep    },
    {SSK("sort"),     Utility::Kind::Sort    },
    {SSK("uniq"),     Utility::Kind::Uniq    },
    {SSK("sleep"),    Utility::Kind::Sleep   },
    {SSK("env"),      Utility::Kind::Env     },
    {SSK("yes"),      Utility::Kind::Yes     },
    {SSK("pkill"),    Utility::Kind::Pkill   },
    {SSK("killall"),  Utility::Kind::Killall },
    {SSK("ps"),       Utility::Kind::Ps      },
    {SSK("make"),     Utility::Kind::Make    },
    {SSK("find"),     Utility::Kind::Find    },
    {SSK("which"),    Utility::Kind::Which   },
    {SSK("whoami"),   Utility::Kind::WhoAmI  },
    {SSK("unlink"),   Utility::Kind::Unlink  },
    {SSK("calc"),     Utility::Kind::Calc    },
};

inline constexpr StaticStringMap<Utility::Kind> SHITBOX_UTILS{
    SHITBOX_ENTRIES, sizeof(SHITBOX_ENTRIES) / sizeof(SHITBOX_ENTRIES[0])};

/* The number of Utility::Kind values, the bound of the per-utility flag-list
   table. */
inline constexpr usize SHITBOX_UTIL_COUNT =
    static_cast<usize>(Utility::Kind::Calc) + 1;

/* The FLAG_LIST of a utility, registered at static-init time by the
   REGISTER_SHITBOX_UTIL_FLAGS line in its file. A utility with no registration
   reads back null. */
fn register_shitbox_util_flags(Utility::Kind chosen,
                               const ArrayList<Flag *> *flags) wontthrow
    -> void;
fn shitbox_util_flag_list(Utility::Kind chosen) wontthrow
    -> const ArrayList<Flag *> *;

#define REGISTER_SHITBOX_UTIL_FLAGS(util)                                      \
  static uchar t__shitbox_flag_registrar =                                     \
      (shit::shitbox::register_shitbox_util_flags(                             \
           shit::shitbox::Utility::Kind::util, &FLAG_LIST),                    \
       0)

/* The utility named, or None when the name is not a shitbox utility. */
fn find_util(StringView name) throws -> Maybe<Utility::Kind>;

/* Every shitbox utility name, for command completion. */
fn util_names() throws -> const ArrayList<String> &;

/* The explicit non-pattern targets of a Makefile, read through the bundled make
   parser. Completion calls this when no GNU make answered the database dump. A
   pattern target and a dot-special are excluded. */
fn collect_makefile_targets(EvalContext &cxt, const Path &makefile) throws
    -> ArrayList<String>;

/* Dispatch the utility whose name sits at args[name_index] of the context. The
   shitbox builtin passes 1 for `shitbox ls` and 0 for a bare-name invocation.
   Reports 127 when the name is not a utility. */
fn dispatch(const ExecContext &ec, EvalContext &cxt, usize name_index) throws
    -> i32;

/* Run a utility chosen by the binary's own basename, the busybox multicall,
   with the shell operands as its arguments. */
fn run_as_multicall(StringView util_name, ArrayList<String> operands,
                    EvalContext &cxt) throws -> i32;

/* The per-utility entry. args is the shifted slice whose first element is the
   utility name, the program name flag parsing reads. */
fn run_util(Utility::Kind chosen, const ExecContext &ec, EvalContext &cxt,
            const ArrayList<String> &args) throws -> i32;

/* Parse a utility's flags and return its real operands, the parse result with
   the leading utility name dropped. */
fn parse_util_operands(const ArrayList<Flag *> &flags,
                       const ArrayList<String> &args) throws
    -> ArrayList<String>;

/* Print a utility's help, the same synopsis and flag table a builtin prints but
   titled with the utility name. */
fn print_util_help(const ExecContext &ec, StringView name, StringView synopsis,
                   StringView description,
                   const ArrayList<Flag *> &flags) throws -> void;

/* Show a utility's help and return zero when its --help flag is set, the mirror
   of the builtin SHOW_BUILTIN_HELP_AND_RETURN. It reads the file's FLAG_HELP,
   HELP_SYNOPSIS, HELP_DESCRIPTION, and FLAG_LIST, and names the utility from
   args[0]. */
#define SHITBOX_SHOW_HELP_AND_RETURN(ec, args)                                 \
  do {                                                                         \
    if (FLAG_HELP.is_enabled()) {                                              \
      shit::shitbox::print_util_help((ec), (args)[0].view(), HELP_SYNOPSIS[0], \
                                     HELP_DESCRIPTION, FLAG_LIST);             \
      return 0;                                                                \
    }                                                                          \
  } while (false)

/* The dispatch mirrors the builtin one. U_CASE constructs the utility and runs
   it, UTILITY_SWITCH_CASES lists every one, and UTILITY_STRUCT declares a class
   per utility deriving from Utility. */
#define U_CASE(util)                                                           \
  case Utility::Kind::util: {                                                  \
    util utility;                                                              \
    return utility.execute(ec, cxt, args);                                     \
  }

#define UTILITY_SWITCH_CASES()                                                 \
  U_CASE(Ls);                                                                  \
  U_CASE(Ln);                                                                  \
  U_CASE(Rm);                                                                  \
  U_CASE(Mkdir);                                                               \
  U_CASE(Rmdir);                                                               \
  U_CASE(Cp);                                                                  \
  U_CASE(Mv);                                                                  \
  U_CASE(Cat);                                                                 \
  U_CASE(Tee);                                                                 \
  U_CASE(Touch);                                                               \
  U_CASE(Basename);                                                            \
  U_CASE(Dirname);                                                             \
  U_CASE(Realpath);                                                            \
  U_CASE(Du);                                                                  \
  U_CASE(Head);                                                                \
  U_CASE(Tail);                                                                \
  U_CASE(Wc);                                                                  \
  U_CASE(Seq);                                                                 \
  U_CASE(Tr);                                                                  \
  U_CASE(Grep);                                                                \
  U_CASE(Sort);                                                                \
  U_CASE(Uniq);                                                                \
  U_CASE(Sleep);                                                               \
  U_CASE(Env);                                                                 \
  U_CASE(Yes);                                                                 \
  U_CASE(Pkill);                                                               \
  U_CASE(Killall);                                                             \
  U_CASE(Ps);                                                                  \
  U_CASE(Make);                                                                \
  U_CASE(Find);                                                                \
  U_CASE(Which);                                                               \
  U_CASE(WhoAmI);                                                              \
  U_CASE(Unlink);                                                              \
  U_CASE(Calc)

#define UTILITY_STRUCT(u)                                                      \
  class u : public Utility                                                     \
  {                                                                            \
  public:                                                                      \
    u();                                                                       \
                                                                               \
    pure Kind kind() const wontthrow override;                                 \
    i32 execute(const ExecContext &ec, EvalContext &cxt,                       \
                const ArrayList<String> &args) const throws override;          \
  };

UTILITY_STRUCT(Ls);
UTILITY_STRUCT(Ln);
UTILITY_STRUCT(Rm);
UTILITY_STRUCT(Mkdir);
UTILITY_STRUCT(Rmdir);
UTILITY_STRUCT(Cp);
UTILITY_STRUCT(Mv);
UTILITY_STRUCT(Cat);
UTILITY_STRUCT(Tee);
UTILITY_STRUCT(Touch);
UTILITY_STRUCT(Basename);
UTILITY_STRUCT(Dirname);
UTILITY_STRUCT(Realpath);
UTILITY_STRUCT(Du);
UTILITY_STRUCT(Head);
UTILITY_STRUCT(Tail);
UTILITY_STRUCT(Wc);
UTILITY_STRUCT(Seq);
UTILITY_STRUCT(Tr);
UTILITY_STRUCT(Grep);
UTILITY_STRUCT(Sort);
UTILITY_STRUCT(Uniq);
UTILITY_STRUCT(Sleep);
UTILITY_STRUCT(Env);
UTILITY_STRUCT(Yes);
UTILITY_STRUCT(Pkill);
UTILITY_STRUCT(Killall);
UTILITY_STRUCT(Ps);
UTILITY_STRUCT(Make);
UTILITY_STRUCT(Find);
UTILITY_STRUCT(Which);
UTILITY_STRUCT(WhoAmI);
UTILITY_STRUCT(Unlink);
UTILITY_STRUCT(Calc);

/* Shared helpers the utilities lean on. read_fd_to_string slurps a descriptor,
   read_named_or_stdin opens a path or reads the command's input for a "-"
   operand. */
fn read_fd_to_string(os::descriptor fd) throws -> String;

/* Remove a path, descending into a directory first when recursive, the shared
   removal the rm and unlink utilities run. Returns false on the first failure
   with the reason in os::last_system_error_message. */
fn remove_path(StringView path, bool is_recursive) throws -> bool;
fn read_named_or_stdin(const ExecContext &ec, StringView path) throws
    -> Maybe<String>;

/* Split text into lines, each element keeping its own trailing newline when the
   source had one, so a rejoin reproduces the bytes. */
fn split_keep_newlines(StringView text) throws -> ArrayList<StringView>;

/* Sort the strings in place by byte order, ascending. */
fn sort_string_list(ArrayList<String> &items) wontthrow -> void;
/* Sort the views in place by byte order, ascending, without copying each into
   an owned String. */
fn sort_stringview_list(ArrayList<StringView> &items) wontthrow -> void;

/* A byte count in the human-readable form ls -h and du -h print, the largest
   1024-based unit whose value is below 1024, with one decimal below ten and a
   K, M, G, T, or P suffix. */
fn format_human_size(u64 bytes) throws -> String;

/* Resolve a signal spelling to its number for pkill and killall, a decimal
   number directly or a name such as TERM or SIGKILL. An empty spelling is the
   TERM default. */
fn resolve_shitbox_signal(StringView spelled) throws -> i32;

/* The numbered signal list the kill family prints under -l, one
   "number) SIGNAME" per line. */
fn format_signal_list() throws -> String;

/* Report a utility error that must not abort the run, with a located caret in
   the default and posix moods and a soft line in the bash mood. A fatal error
   throws an Error instead. */
fn report_soft_shitbox_error(const ExecContext &ec, EvalContext &cxt,
                             StringView message) throws -> void;

} // namespace shitbox

} // namespace shit
