#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Utils.hpp"

/* type reports how each name resolves, checking the same order the shell uses
   to run a command, a function first, then a builtin, then the PATH. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("name [name ...]");

HELP_DESCRIPTION_DECL(
    "The type builtin reports how each name resolves, checking the order the "
    "shell uses to run a command, a keyword first, then an alias, then a "
    "function, then a builtin, then the PATH. The status is non-zero when any "
    "name resolves to nothing.");

FLAG(TYPE_WORD, Bool, 't', "",
     "Print only the word naming the type, such as builtin or file.");
FLAG(TYPE_PATH, Bool, 'p', "",
     "Print the disk path, or nothing when the name is not a file.");
FLAG(TYPE_FORCE_PATH, Bool, 'P', "",
     "Search the PATH and print the disk path even for a name that is also a "
     "builtin.");
FLAG(TYPE_ALL, Bool, 'a', "",
     "Print every location of each name, the keyword, alias, function, or "
     "builtin and every matching file on the PATH.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

namespace shit {

Type::Type() = default;

pure Builtin::Kind Type::kind() const wontthrow { return Kind::Type; }

i32 Type::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  ASSERT(!args.is_empty());

  let const want_word = FLAG_TYPE_WORD.is_enabled();
  let const want_path = FLAG_TYPE_PATH.is_enabled();
  let const force_path = FLAG_TYPE_FORCE_PATH.is_enabled();

  let out = String{};
  bool all_found = true;

  for (usize i = 1; i < args.count(); i++) {
    let const &name = args[i];

    /* -P forces a PATH lookup and ignores the keyword, alias, function, and
       builtin classes, so it reports the disk file even when the name also names
       a builtin. */
    if (force_path) {
      if (let const paths = utils::search_program_path(name);
          paths.count() != 0)
      {
        if (want_word)
          out += "file";
        else
          out += paths[0].text();
        out += "\n";
      } else {
        all_found = false;
      }
      continue;
    }

    /* The classification follows the shell's own resolution order. The word the
       -t form prints names each class, while the default form spells it out. */
    StringView word{};
    Maybe<String> alias_value;
    if (utils::is_posix_reserved_word(name.view())) {
      word = "keyword";
    } else if (let const alias = cxt.get_alias(name.view());
               alias.has_value()) {
      word = "alias";
      alias_value = alias;
    } else if (cxt.has_functions() && cxt.find_function(name) != nullptr) {
      word = "function";
    } else if (search_builtin(name.view()).has_value()) {
      word = "builtin";
    }

    /* -a prints every location, the class word followed by each PATH match,
       which is what type -at does to enumerate a name's resolutions. */
    if (FLAG_TYPE_ALL.is_enabled()) {
      bool any = false;
      if (!word.is_empty()) {
        any = true;
        if (want_word) {
          out += word;
          out += "\n";
        } else if (!want_path) {
          out += name;
          if (word == "alias") {
            out += " is an alias for ";
            out += *alias_value;
          } else if (word == "keyword") {
            out += " is a shell keyword";
          } else if (word == "function") {
            out += " is a shell function";
          } else {
            out += " is a shell builtin";
          }
          out += "\n";
        }
      }
      for (const Path &path : utils::search_program_path(name)) {
        any = true;
        if (want_word) {
          out += "file\n";
        } else if (want_path) {
          out += path.text();
          out += "\n";
        } else {
          out += name;
          out += " is ";
          out += path.text();
          out += "\n";
        }
      }
      if (!any) {
        if (!want_word && !want_path) {
          out += name;
          out += ": not found\n";
        }
        all_found = false;
      }
      continue;
    }

    if (!word.is_empty()) {
      /* A keyword, alias, function, or builtin is not a disk file, so -p prints
         nothing for it. */
      if (want_word) {
        out += word;
        out += "\n";
      } else if (!want_path) {
        out += name;
        if (word == "alias") {
          out += " is an alias for ";
          out += *alias_value;
        } else if (word == "keyword") {
          out += " is a shell keyword";
        } else if (word == "function") {
          out += " is a shell function";
        } else {
          out += " is a shell builtin";
        }
        out += "\n";
      }
      continue;
    }

    if (let const paths = utils::search_program_path(name);
        paths.count() != 0)
    {
      if (want_word) {
        out += "file\n";
      } else if (want_path) {
        out += paths[0].text();
        out += "\n";
      } else {
        out += name;
        out += " is ";
        out += paths[0].text();
        out += "\n";
      }
    } else {
      /* -t and -p stay silent for a name that resolves to nothing, only the
         status reports it. */
      if (!want_word && !want_path) {
        out += name;
        out += ": not found\n";
      }
      all_found = false;
    }
  }

  ec.print_to_stdout(out);
  return all_found ? 0 : 1;
}

} /* namespace shit */
