#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Shitbox.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-rRv] source ... destination");

HELP_DESCRIPTION_DECL(
    "The cp utility copies each source to the destination. With -r it copies a "
    "directory and its contents. When more than one source is given the "
    "destination must be a directory. With -v it names each copy as it "
    "happens.");

FLAG(CP_RECURSIVE_R, Bool, 'r', "", "Copy directories and their contents.");
FLAG(CP_RECURSIVE_UPPER, Bool, 'R', "", "Copy directories and their contents.");
FLAG(CP_VERBOSE, Bool, 'v', "", "Print the name of each copy as it happens.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

namespace shitbox {

/* Copy one regular file's bytes through the descriptor layer, so no file stream
   is pulled in, and name the copy on the verbose output. Throws a located error
   naming the path that failed. */
static fn copy_file(const ExecContext &ec, StringView source,
                    StringView destination, bool verbose) throws -> void
{
  let const in_fd = os::open_file_descriptor(source, os::file_open_mode::Read);
  if (!in_fd.has_value())
    throw Error{"cp: unable to open '" + String{source} + "' because " +
                os::last_system_error_message()};
  defer { os::close_fd(*in_fd); };

  let const out_fd =
      os::open_file_descriptor(destination, os::file_open_mode::Truncate);
  if (!out_fd.has_value())
    throw Error{"cp: unable to create '" + String{destination} + "' because " +
                os::last_system_error_message()};
  defer { os::close_fd(*out_fd); };

  char buffer[4096];
  for (;;) {
    let const read_count = os::read_fd(*in_fd, buffer, sizeof(buffer));
    if (!read_count.has_value())
      throw Error{"cp: a read of '" + String{source} + "' failed"};
    if (*read_count == 0) break;
    let const written = os::write_fd(*out_fd, buffer, *read_count);
    if (!written.has_value() || *written != *read_count)
      throw Error{"cp: a write to '" + String{destination} + "' failed"};
  }

  if (verbose)
    ec.print_to_stdout("'" + String{source} + "' -> '" + String{destination} +
                       "'\n");
}

/* Copy a file or, with the recursive flag, a whole directory tree. */
static fn copy_path(const ExecContext &ec, StringView source,
                    StringView destination, bool recursive, bool verbose) throws
    -> void
{
  let const source_path = Path{source};
  /* A symlink is copied as a leaf rather than descended into, the way rm and du
     guard their recursion, so a symlink that points back into the tree does not
     drive an unbounded walk. */
  if (source_path.is_directory() && !source_path.is_symbolic_link()) {
    if (!recursive)
      throw Error{"cp: '" + String{source} +
                  "' is a directory, pass -r to copy it"};
    os::make_directory(destination, 0777);
    Maybe<ArrayList<String>> names = Path::read_directory(source_path);
    if (!names.has_value())
      throw Error{"cp: unable to read the directory '" + String{source} + "'"};
    for (const String &name : *names) {
      let const child_source = PathBuilder{source}.append(name.view()).build();
      let const child_destination =
          PathBuilder{destination}.append(name.view()).build();
      copy_path(ec, child_source.text().view(),
                child_destination.text().view(), recursive, verbose);
    }
    return;
  }
  copy_file(ec, source, destination, verbose);
}

fn util_cp(const ExecContext &ec, EvalContext &cxt,
           const ArrayList<String> &args) throws -> i32
{
  unused(cxt);
  let const operands = parse_util_operands(FLAG_LIST, args);
  defer { reset_flags(FLAG_LIST); };

  if (FLAG_HELP.is_enabled()) {
    print_util_help(ec, args[0].view(), HELP_SYNOPSIS[0], HELP_DESCRIPTION,
                    FLAG_LIST);
    return 0;
  }

  if (operands.count() < 2)
    throw Error{"cp: a source and a destination operand are required"};

  let const recursive =
      FLAG_CP_RECURSIVE_R.is_enabled() || FLAG_CP_RECURSIVE_UPPER.is_enabled();
  let const verbose = FLAG_CP_VERBOSE.is_enabled();
  let const destination = operands[operands.count() - 1].view();
  let const destination_is_directory = Path{destination}.is_directory();

  if (operands.count() > 2 && !destination_is_directory)
    throw Error{"cp: the destination '" + String{destination} +
                "' is not a directory, so it cannot hold several sources"};

  for (usize i = 0; i + 1 < operands.count(); i++) {
    let const source = operands[i].view();
    /* A copy into a directory keeps the source basename, so cp a b dir/ writes
       dir/a and dir/b. */
    if (destination_is_directory) {
      let const leaf = Path{source}.filename();
      let const target = PathBuilder{destination}.append(leaf).build();
      copy_path(ec, source, target.text().view(), recursive, verbose);
    } else {
      copy_path(ec, source, destination, recursive, verbose);
    }
  }
  return 0;
}

} /* namespace shitbox */

} /* namespace shit */
