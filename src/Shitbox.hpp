#pragma once

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
   and a make under shit. The utilities mirror the builtin registry, a frozen
   StaticStringMap from a name to a Util, dispatched through a switch. A utility
   is reached three ways. The shitbox builtin runs `shitbox ls`, the bare name
   ls resolves directly when the toggle is on, and a binary renamed to ls acts
   as the utility through the multicall entry. */
namespace shitbox {

enum class Util : u8
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
  Make,
};

inline constexpr StaticStringMap<Util>::entry SHITBOX_ENTRIES[] = {
    {PackedStringKey::from_literal("ls"),       Util::Ls      },
    {PackedStringKey::from_literal("ln"),       Util::Ln      },
    {PackedStringKey::from_literal("rm"),       Util::Rm      },
    {PackedStringKey::from_literal("mkdir"),    Util::Mkdir   },
    {PackedStringKey::from_literal("rmdir"),    Util::Rmdir   },
    {PackedStringKey::from_literal("cp"),       Util::Cp      },
    {PackedStringKey::from_literal("mv"),       Util::Mv      },
    {PackedStringKey::from_literal("cat"),      Util::Cat     },
    {PackedStringKey::from_literal("tee"),      Util::Tee     },
    {PackedStringKey::from_literal("touch"),    Util::Touch   },
    {PackedStringKey::from_literal("basename"), Util::Basename},
    {PackedStringKey::from_literal("dirname"),  Util::Dirname },
    {PackedStringKey::from_literal("realpath"), Util::Realpath},
    {PackedStringKey::from_literal("du"),       Util::Du      },
    {PackedStringKey::from_literal("head"),     Util::Head    },
    {PackedStringKey::from_literal("tail"),     Util::Tail    },
    {PackedStringKey::from_literal("wc"),       Util::Wc      },
    {PackedStringKey::from_literal("seq"),      Util::Seq     },
    {PackedStringKey::from_literal("tr"),       Util::Tr      },
    {PackedStringKey::from_literal("grep"),     Util::Grep    },
    {PackedStringKey::from_literal("sort"),     Util::Sort    },
    {PackedStringKey::from_literal("uniq"),     Util::Uniq    },
    {PackedStringKey::from_literal("sleep"),    Util::Sleep   },
    {PackedStringKey::from_literal("env"),      Util::Env     },
    {PackedStringKey::from_literal("yes"),      Util::Yes     },
    {PackedStringKey::from_literal("pkill"),    Util::Pkill   },
    {PackedStringKey::from_literal("killall"),  Util::Killall },
    {PackedStringKey::from_literal("make"),     Util::Make    },
};

inline constexpr StaticStringMap<Util> SHITBOX_UTILS{
    SHITBOX_ENTRIES, sizeof(SHITBOX_ENTRIES) / sizeof(SHITBOX_ENTRIES[0])};

/* The utility named, or None when the name is not a shitbox utility. The
   command resolver and the multicall entry both ask through this. */
fn find_util(StringView name) throws -> Maybe<Util>;

/* Every shitbox utility name, recovered once from the table, for command
   completion to offer them alongside the builtins. */
fn util_names() throws -> const ArrayList<String> &;

/* Whether the bare utility names resolve as commands, set by the
   --enable-shitbox flag at startup and the set -o shitbox option at run time.
   The command resolver reads this without an EvalContext in scope, so the state
   lives in a process global the option mirrors onto. */
fn shitbox_names_enabled() wontthrow -> bool;
fn set_shitbox_names_enabled(bool enabled) wontthrow -> void;

/* Dispatch the utility whose name sits at args[name_index] of the context. The
   shitbox builtin passes 1 for `shitbox ls` and 0 for a bare-name invocation,
   so the utility always sees its own name at the front of the slice it reads.
   Reports 127 when the name is not a utility. */
fn dispatch(const ExecContext &ec, EvalContext &cxt, usize name_index) throws
    -> i32;

/* Run a utility chosen by the binary's own basename, the busybox multicall,
   with the shell operands as its arguments. Builds its own context and reports
   the utility's status, or a non-zero status with a message on a thrown error.
 */
fn run_as_multicall(StringView util_name, ArrayList<String> operands,
                    EvalContext &cxt) throws -> i32;

/* The per-utility entry. args is the shifted slice whose first element is the
   utility name, so flag parsing treats it as the program name the way a builtin
   reads ec.args(). The context carries the descriptors and the print helpers.
 */
fn run_util(Util chosen, const ExecContext &ec, EvalContext &cxt,
            const ArrayList<String> &args) throws -> i32;

/* Parse a utility's flags and return its real operands, the parse result with
   the leading utility name dropped. parse_flags_vec keeps the program name as
   the first operand the way a builtin reads it, so a utility that wants only
   its arguments calls this. */
fn parse_util_operands(const ArrayList<Flag *> &flags,
                       const ArrayList<String> &args) throws
    -> ArrayList<String>;

/* Print a utility's help, the same synopsis and flag table a builtin prints but
   titled with the utility name rather than the shitbox builtin name. */
fn print_util_help(const ExecContext &ec, StringView name, StringView synopsis,
                   StringView description,
                   const ArrayList<Flag *> &flags) throws -> void;

/* Show a utility's help and return zero when its --help flag is set, the mirror
   of the builtin SHOW_BUILTIN_HELP_AND_RETURN so each utility states the check
   once. It reads the file's FLAG_HELP, HELP_SYNOPSIS, HELP_DESCRIPTION, and
   FLAG_LIST, and names the utility from args[0]. */
#define SHITBOX_SHOW_HELP_AND_RETURN(ec, args)                                 \
  do {                                                                         \
    if (FLAG_HELP.is_enabled()) {                                              \
      shit::shitbox::print_util_help((ec), (args)[0].view(), HELP_SYNOPSIS[0], \
                                     HELP_DESCRIPTION, FLAG_LIST);             \
      return 0;                                                                \
    }                                                                          \
  } while (false)

#define SHITBOX_DECLARE_UTIL(name)                                             \
  fn name(const ExecContext &ec, EvalContext &cxt,                             \
          const ArrayList<String> &args) throws -> i32

SHITBOX_DECLARE_UTIL(util_ls);
SHITBOX_DECLARE_UTIL(util_ln);
SHITBOX_DECLARE_UTIL(util_rm);
SHITBOX_DECLARE_UTIL(util_mkdir);
SHITBOX_DECLARE_UTIL(util_rmdir);
SHITBOX_DECLARE_UTIL(util_cp);
SHITBOX_DECLARE_UTIL(util_mv);
SHITBOX_DECLARE_UTIL(util_cat);
SHITBOX_DECLARE_UTIL(util_tee);
SHITBOX_DECLARE_UTIL(util_touch);
SHITBOX_DECLARE_UTIL(util_basename);
SHITBOX_DECLARE_UTIL(util_dirname);
SHITBOX_DECLARE_UTIL(util_realpath);
SHITBOX_DECLARE_UTIL(util_du);
SHITBOX_DECLARE_UTIL(util_head);
SHITBOX_DECLARE_UTIL(util_tail);
SHITBOX_DECLARE_UTIL(util_wc);
SHITBOX_DECLARE_UTIL(util_seq);
SHITBOX_DECLARE_UTIL(util_tr);
SHITBOX_DECLARE_UTIL(util_grep);
SHITBOX_DECLARE_UTIL(util_sort);
SHITBOX_DECLARE_UTIL(util_uniq);
SHITBOX_DECLARE_UTIL(util_sleep);
SHITBOX_DECLARE_UTIL(util_env);
SHITBOX_DECLARE_UTIL(util_yes);
SHITBOX_DECLARE_UTIL(util_pkill);
SHITBOX_DECLARE_UTIL(util_killall);
SHITBOX_DECLARE_UTIL(util_make);

/* Shared helpers the utilities lean on, so each utility file stays small.
   read_fd_to_string slurps a descriptor, read_named_or_stdin opens a path or
   reads the command's input for a "-" operand, and write_all_to_stdout sends a
   buffer to the command's standard output. */
fn read_fd_to_string(os::descriptor fd) throws -> String;
fn read_named_or_stdin(const ExecContext &ec, StringView path) throws
    -> Maybe<String>;

/* Split text into lines, each element keeping its own trailing newline when the
   source had one, so a rejoin reproduces the bytes. A final line without a
   newline is kept as its own element. */
fn split_keep_newlines(StringView text) throws -> ArrayList<StringView>;

/* Sort the strings in place by byte order, ascending, the order ls and sort
   print in. */
fn sort_string_list(ArrayList<String> &items) wontthrow -> void;

/* A byte count in the human-readable form ls -h and du -h print, the largest
   1024-based unit whose value is below 1024, with one decimal below ten and a
   K, M, G, or T suffix. A value below 1024 prints as the plain byte count. */
fn format_human_size(u64 bytes) throws -> String;

/* Resolve a signal spelling to its number for pkill and killall, a decimal
   number directly or a name such as TERM or SIGKILL through the platform table.
   An empty spelling is the TERM default. */
fn resolve_shitbox_signal(StringView spelled) throws -> i32;

/* Report a utility error that must not abort the run, such as one bad operand
   in a list the utility still finishes, with a located caret in the default and
   posix moods and a soft line in the bash mood. A fatal error throws an Error
   instead, which the builtin dispatch relocates the same way. */
fn report_soft_shitbox_error(const ExecContext &ec, EvalContext &cxt,
                             StringView message) throws -> void;

} /* namespace shitbox */

} /* namespace shit */
