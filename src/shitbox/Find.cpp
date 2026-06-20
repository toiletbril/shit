#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Shitbox.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL(
    "[path ...] [-name glob] [-type fdl] [-maxdepth n] [-mindepth n]");

HELP_DESCRIPTION_DECL(
    "The find utility walks each path operand, or the current directory when "
    "none is given, and prints every entry under it. The -name predicate keeps "
    "the entries whose final component matches a glob, -type keeps a file, a "
    "directory, or a symlink, and -maxdepth and -mindepth bound the walk "
    "depth.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Find);

namespace shit {

namespace shitbox {

/* The predicates one find invocation filters on, parsed once from the command
   line and read at every entry the walk visits. The name pattern points into
   the argument that spelled it, and glob_active marks every byte of that
   pattern as a live metacharacter the way an unquoted glob acts. */
struct find_options
{
  bool has_name{false};
  StringView name_pattern{};
  const ArrayList<bool> *glob_active{nullptr};
  char type_filter{0};
  i64 max_depth{-1};
  i64 min_depth{0};
};

/* Whether the entry of this type at this depth satisfies every predicate, so
   the walk prints it. The depth bounds come first, then the type, then the name
   glob. The type letter comes from the one stat the walk already took, where a
   regular file reads as a dash the way the mode string spells it. */
static fn find_entry_matches(char type_letter, StringView filename, usize depth,
                             const find_options &options) throws -> bool
{
  if (static_cast<i64>(depth) < options.min_depth) return false;
  if (options.max_depth >= 0 && static_cast<i64>(depth) > options.max_depth) {
    return false;
  }

  if (options.type_filter == 'f' && type_letter != '-') {
    return false;
  }
  if (options.type_filter == 'd' && type_letter != 'd') {
    return false;
  }
  if (options.type_filter == 'l' && type_letter != 'l') {
    return false;
  }

  if (options.has_name && !utils::glob_matches(options.name_pattern, filename,
                                               *options.glob_active, 0))
  {
    return false;
  }

  return true;
}

/* Print the entry when it matches, then descend into a directory while the
   depth stays under the maximum. The children are walked in sorted order so the
   output is stable. */
static fn find_walk(const Path &path, StringView display, usize depth,
                    const find_options &options, String &output) throws -> void
{
  /* One stat serves both the type match and the descend decision. It reads as a
     symlink rather than its target, so the walk does not follow a symlink the
     way the default find does not. A stat that fails yields the unknown marker
     0, which matches no -type filter and is not descended, so a broken entry is
     never miscounted as a regular file. */
  os::file_status status{};
  const char type_letter = os::stat_path(path.text().view(), status)
                               ? os::file_type_letter(status.mode)
                               : '\0';

  if (find_entry_matches(type_letter, path.filename(), depth, options)) {
    output += display;
    output += '\n';
  }

  const bool should_descend =
      options.max_depth < 0 || static_cast<i64>(depth) < options.max_depth;
  if (!should_descend || type_letter != 'd') {
    return;
  }

  Maybe<ArrayList<String>> names = Path::read_directory(path);
  if (!names.has_value()) return;
  sort_string_list(*names);

  for (let const &child_name : *names) {
    String child_display{display};
    if (!child_display.is_empty() && child_display.back() != '/') {
      child_display += '/';
    }
    child_display += child_name.view();
    let const child_path = Path{child_display.view()};
    find_walk(child_path, child_display.view(), depth + 1, options, output);
  }
}

/* The integer operand of a depth predicate, or a located error when it is
   missing or not a number. */
static fn parse_depth_argument(const ArrayList<String> &args, usize index,
                               StringView predicate) throws -> i64
{
  if (index >= args.count())
    throw Error{"find: " + String{predicate} + " expects a number"};

  /* A negative value is rejected rather than parsed, since max_depth carries -1
     as its no-limit sentinel, so a negative -maxdepth would otherwise read as
     an unbounded walk rather than the error find gives. */
  let const parsed_value = utils::parse_decimal_integer(args[index].view());
  if (parsed_value.is_error() || parsed_value.value() < 0) {
    throw Error{"find: " + String{predicate} +
                " expects a non-negative number, got '" + args[index] + "'"};
  }

  return parsed_value.value();
}

Find::Find() = default;

pure Utility::Kind Find::kind() const wontthrow { return Kind::Find; }

fn Find::execute(const ExecContext &ec, EvalContext &cxt,
                 const ArrayList<String> &args) const throws -> i32
{
  unused(cxt);

  ArrayList<StringView> roots{};
  find_options options{};
  ArrayList<bool> name_glob_active{};

  /* The path operands lead, every token up to the first predicate, then the
     dash predicates follow the way find reads its command line. The flag parser
     is bypassed, since a predicate such as -name is not a single-letter flag
     bundle. */
  usize index = 1;
  while (index < args.count() && !args[index].view().is_empty() &&
         args[index].view()[0] != '-')
  {
    roots.push(args[index].view());
    index++;
  }

  for (; index < args.count(); index++) {
    let const predicate = args[index].view();
    if (predicate == "--help") {
      print_util_help(ec, args[0].view(), HELP_SYNOPSIS[0], HELP_DESCRIPTION,
                      FLAG_LIST);
      return 0;
    } else if (predicate == "-print") {
      /* The walk prints every matched entry already, so -print is the default
         and needs no action. */
    } else if (predicate == "-name") {
      if (index + 1 >= args.count())
        throw Error{"find: -name expects a pattern"};
      options.has_name = true;
      options.name_pattern = args[index + 1].view();
      index++;
    } else if (predicate == "-type") {
      if (index + 1 >= args.count())
        throw Error{"find: -type expects one of f, d, or l"};
      let const type = args[index + 1].view();
      if (type.length != 1 ||
          (type[0] != 'f' && type[0] != 'd' && type[0] != 'l'))
      {
        throw Error{"find: -type expects one of f, d, or l"};
      }
      options.type_filter = type[0];
      index++;
    } else if (predicate == "-maxdepth") {
      options.max_depth = parse_depth_argument(args, index + 1, predicate);
      index++;
    } else if (predicate == "-mindepth") {
      options.min_depth = parse_depth_argument(args, index + 1, predicate);
      index++;
    } else {
      throw Error{"find: unknown predicate '" + String{predicate} + "'"};
    }
  }

  if (options.has_name) {
    name_glob_active.reserve(options.name_pattern.length);
    for (usize i = 0; i < options.name_pattern.length; i++)
      name_glob_active.push(true);
    options.glob_active = &name_glob_active;
  }

  if (roots.is_empty()) roots.push(StringView{"."});

  let output = String{};
  i32 status = 0;
  for (let const &root : roots) {
    let const root_path = Path{root};
    if (!root_path.exists()) {
      report_soft_shitbox_error(
          ec, cxt, "find: '" + String{root} + "': no such file or directory");
      status = 1;
      continue;
    }
    find_walk(root_path, root, 0, options, output);
  }

  ec.print_to_stdout(output);
  return status;
}

} /* namespace shitbox */

} /* namespace shit */
