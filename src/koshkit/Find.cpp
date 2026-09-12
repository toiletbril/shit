/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the find utility. It parses name, type, depth, and print
 * predicates, then walks directory trees without following symbolic links.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Path.hpp"
#include "../StaticStringMap.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL(
    "[path ...] [-name glob] [-type fdl] [-maxdepth n] [-mindepth n]");

HELP_DESCRIPTION_DECL(
    "The find utility walks each path and prints every entry under it.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_KOSHKIT_UTIL_FLAGS(Find);

namespace koshka {

namespace koshkit {

struct find_options
{
  const ArrayList<StringView> *name_patterns{nullptr};
  const Bitset *glob_active{nullptr};
  char type_filter{0};
  i64 max_depth{-1};
  i64 min_depth{0};
};

enum class find_predicate_kind : uchar
{
  Help,
  Print,
  Name,
  Type,
  MaximumDepth,
  MinimumDepth,
};

static constexpr static_string_entry<find_predicate_kind>
    FIND_PREDICATE_ENTRIES[] = {
        {SSK("--help"),    find_predicate_kind::Help        },
        {SSK("-print"),    find_predicate_kind::Print       },
        {SSK("-name"),     find_predicate_kind::Name        },
        {SSK("-type"),     find_predicate_kind::Type        },
        {SSK("-maxdepth"), find_predicate_kind::MaximumDepth},
        {SSK("-mindepth"), find_predicate_kind::MinimumDepth},
};
static constexpr StaticStringMap FIND_PREDICATES{FIND_PREDICATE_ENTRIES};

static fn find_entry_matches(char type_letter, StringView filename, usize depth,
                             const find_options &options) throws -> bool
{
  if (static_cast<i64>(depth) < options.min_depth) return false;
  if (options.max_depth >= 0 && static_cast<i64>(depth) > options.max_depth) {
    return false;
  }

  switch (options.type_filter) {
  case 'f':
    if (type_letter != '-') return false;
    break;
  case 'd':
    if (type_letter != 'd') return false;
    break;
  case 'l':
    if (type_letter != 'l') return false;
    break;
  default: break;
  }

  if (options.name_patterns != nullptr) {
    for (let const &pattern : *options.name_patterns) {
      if (!utils::glob_matches(pattern, filename, *options.glob_active, 0))
        return false;
    }
  }

  return true;
}

static fn find_walk(const ExecContext &ec, EvalContext &cxt, const Path &path,
                    StringView display, usize depth,
                    const find_options &options, String &output,
                    i32 &exit_status, Allocator allocator,
                    const os::file_status *known_status = nullptr) throws
    -> void
{
  /* The stat reads the symlink, not its target, and a failed stat yields the
     marker '\0' that matches no -type filter and is not descended. */
  os::file_status queried_status{};
  if (known_status == nullptr &&
      os::stat_path(path.text().view(), queried_status))
  {
    known_status = &queried_status;
  }
  let const type_letter =
      known_status != nullptr ? os::file_type_letter(known_status->mode) : '\0';

  if (find_entry_matches(type_letter, path.filename(), depth, options)) {
    output += display;
    output += '\n';
  }

  let const should_descend =
      options.max_depth < 0 || static_cast<i64>(depth) < options.max_depth;
  if (!should_descend || type_letter != 'd') {
    return;
  }

  let children = os::list_directory_status(path.text().view(), allocator);
  if (!children.has_value()) {
    if (!path.is_readable()) {
      report_soft_koshkit_error(ec, cxt,
                                "find: '" + String{allocator, display} +
                                    "': Permission denied");
      exit_status = 1;
    }

    return;
  }
  children->sort([](const os::directory_status_entry &left,
                    const os::directory_status_entry &right) {
    return left.child.name.view() < right.child.name.view();
  });

  for (let const &child_entry : *children) {
    if (os::INTERRUPT_REQUESTED) return;

    String child_display{allocator, display};
    if (!child_display.is_empty() && child_display.back() != '/') {
      child_display += '/';
    }
    child_display += child_entry.child.name.view();
    let const child_path = Path{child_display.view()};
    let const child_status =
        child_entry.has_status ? &child_entry.status : nullptr;
    find_walk(ec, cxt, child_path, child_display.view(), depth + 1, options,
              output, exit_status, allocator, child_status);
  }
}

static fn parse_depth_argument(const ArrayList<String> &args, usize index,
                               StringView predicate, Allocator allocator) throws
    -> i64
{
  if (index >= args.count())
    throw Error{
        "find: " + String{allocator, predicate}
          + " expects a number"
    };

  /* A negative value is rejected rather than parsed, since max_depth carries -1
     as its no-limit sentinel, so a negative -maxdepth would otherwise read as
     an unbounded walk rather than the error find gives. */
  let const parsed_value = args[index].view().to<i64>();
  if (parsed_value.is_error() || parsed_value.value() < 0) {
    throw Error{
        "find: " + String{allocator, predicate}
          +
        " expects a non-negative number, got '" + args[index] + "'"
    };
  }

  return parsed_value.value();
}

Find::Find() = default;

pure fn Find::kind() const wontthrow -> Utility::Kind { return Kind::Find; }

fn Find::execute(const ExecContext &ec, EvalContext &cxt,
                 const ArrayList<String> &args,
                 const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  unused(arg_locations);

  ArrayList<StringView> roots{cxt.scratch_allocator()};
  ArrayList<StringView> name_patterns{cxt.scratch_allocator()};
  find_options options{};
  Bitset name_glob_active{cxt.scratch_allocator()};

  /* The flag parser is bypassed, a predicate such as -name is not a
     single-letter flag bundle. An empty argument is a start path, not a
     predicate, so it is collected as a root. */
  usize index = 1;
  while (index < args.count()) {
    let const start_argument = args[index].view();
    if (!start_argument.is_empty() && start_argument[0] == '-') {
      break;
    }

    roots.push(start_argument);
    index++;
  }

  for (; index < args.count(); index++) {
    let const predicate = args[index].view();
    let const predicate_kind = FIND_PREDICATES.find(predicate);
    if (!predicate_kind.has_value())
      throw Error{
          "find: unknown predicate '" +
          String{cxt.scratch_allocator(), predicate}
          + "'"
      };

    switch (*predicate_kind) {
    case find_predicate_kind::Help:
      print_util_help(ec, args[0].view(), HELP_SYNOPSIS[0], HELP_DESCRIPTION,
                      FLAG_LIST);
      return 0;
    case find_predicate_kind::Print:
      /* The walk prints every matched entry already, so -print is the default
         and needs no action. */
      break;
    case find_predicate_kind::Name:
      if (index + 1 >= args.count())
        throw ErrorWithDetails{"find: -name expects a pattern",
                               "Pass a glob after `-name`, e.g. `-name '*.c'`"};
      name_patterns.push(args[index + 1].view());
      index++;
      break;
    case find_predicate_kind::Type: {
      if (index + 1 >= args.count())
        throw ErrorWithDetails{"find: -type expects one of f, d, or l",
                               "Pass `f`, `d`, or `l` after `-type`"};
      let const type = args[index + 1].view();
      if (type.length != 1 ||
          (type[0] != 'f' && type[0] != 'd' && type[0] != 'l'))
      {
        throw ErrorWithDetails{"find: -type expects one of f, d, or l",
                               "Pass `f`, `d`, or `l` after `-type`"};
      }
      options.type_filter = type[0];
      index++;
      break;
    }
    case find_predicate_kind::MaximumDepth:
      options.max_depth = parse_depth_argument(args, index + 1, predicate,
                                               cxt.scratch_allocator());
      index++;
      break;
    case find_predicate_kind::MinimumDepth:
      options.min_depth = parse_depth_argument(args, index + 1, predicate,
                                               cxt.scratch_allocator());
      index++;
      break;
    }
  }

  if (!name_patterns.is_empty()) {
    usize longest_pattern_length = 0;
    for (let const &pattern : name_patterns) {
      if (pattern.length > longest_pattern_length)
        longest_pattern_length = pattern.length;
    }

    name_glob_active.reserve(longest_pattern_length);
    for (usize i = 0; i < longest_pattern_length; i++)
      name_glob_active.push(true);

    options.name_patterns = &name_patterns;
    options.glob_active = &name_glob_active;
  }

  if (roots.is_empty()) roots.push(StringView{"."});

  let const allocator = cxt.scratch_allocator();
  let root_paths = ArrayList<Path>{allocator};
  let root_statuses = ArrayList<os::file_status>{allocator};
  let batch = os::Batch{allocator};
  root_paths.reserve(roots.count());
  root_statuses.reserve(roots.count());
  batch.reserve(roots.count());
  for (let const &root : roots) {
    root_paths.push(Path{root});
    root_statuses.push({});
  }
  for (usize root_index = 0; root_index < roots.count(); root_index++) {
    batch.add(os::batch_operation::lstat(root_paths[root_index],
                                         root_statuses[root_index]));
  }
  let const results = batch.execute();

  let output = String{allocator};
  i32 status = 0;
  for (usize root_index = 0; root_index < roots.count(); root_index++) {
    let const root = roots[root_index];
    if (results[root_index].error_number != 0) {
      report_soft_koshkit_error(ec, cxt,
                                "find: '" + String{allocator, root} +
                                    "': no such file or directory");
      status = 1;
      continue;
    }
    find_walk(ec, cxt, root_paths[root_index], root, 0, options, output, status,
              allocator, &root_statuses[root_index]);
  }

  ec.print_to_stdout(output);
  return status;
}

} /* namespace koshkit */

} /* namespace koshka */
