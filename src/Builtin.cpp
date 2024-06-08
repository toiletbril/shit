#include "Builtin.hpp"

#include "Cli.hpp"
#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Toiletline.hpp"
#include "Utils.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>

namespace shit {

static const std::unordered_map<std::string, Builtin::Kind> BUILTINS = {
    {"echo", Builtin::Kind::Echo},
    {"exit", Builtin::Kind::Exit},
    {"cd",   Builtin::Kind::Cd  },
};

Builtin::Kind
search_builtin(std::string_view builtin_name)
{
  std::string lower_builtin_name;
  for (const uchar c : builtin_name)
    lower_builtin_name += std::tolower(c);

  if (auto b = BUILTINS.find(lower_builtin_name.c_str()); b != BUILTINS.end()) {
    return b->second;
  }

  return Builtin::Kind::Invalid;
}

i32
execute_builtin(Builtin::Kind kind, const std::vector<std::string> &args)
{
  SHIT_ASSERT(kind != Builtin::Kind::Invalid);

  std::unique_ptr<Builtin> b{};

  switch (kind) {
    /* clang-format off */
  case Builtin::Kind::Echo: return Echo{}.execute(args); break;
  case Builtin::Kind::Cd:   return Cd{}.execute(args); break;
  case Builtin::Kind::Exit: return Exit{}.execute(args); break;
    /* clang-format on */

  default: break;
  }

  SHIT_UNREACHABLE("Unhandled builtin of type %d", E(kind));
}

/**
 * class: Builtin
 */
Builtin::Builtin() = default;

/**
 * class: Echo
 */
Echo::Echo() = default;

Builtin::Kind
Echo::kind() const
{
  return Kind::Echo;
}

i32
Echo::execute(const std::vector<std::string> &args) const
{
  std::string buf;

  if (args.size() > 0) {
    buf += args[0];
    for (usize i = 1; i < args.size(); i++) {
      buf += ' ';
      buf += args[i];
    }
  }

  std::cout << buf << std::endl;

  return 0;
}

/**
 * class: Cd
 */
Cd::Cd() = default;

Builtin::Kind
Cd::kind() const
{
  return Kind::Cd;
}

i32
Cd::execute(const std::vector<std::string> &args) const
{
  std::string arg_path;

  if (args.size() > 0) {
    arg_path += args[0];
    for (usize i = 1; i < args.size(); i++) {
      arg_path += ' ';
      arg_path += args[i];
    }
  } else {
    /* Empty cd should go to the parent directory. */
    arg_path = "..";
  }

  std::filesystem::path np{arg_path};
  np = std::filesystem::absolute(np).lexically_normal();

  if (std::filesystem::exists(np)) {
    utils::set_current_directory(np);
    return 0;
  }

  throw Error{"Path '" + arg_path + "' does not exist"};
}

/**
 * class: Exit
 */
Exit::Exit() = default;

Builtin::Kind
Exit::kind() const
{
  return Kind::Exit;
}

i32
Exit::execute(const std::vector<std::string> &args) const
{
  utils::quit(args.size() > 0 ? std::atoi(args[0].c_str()) : 0, true);
}

} /* namespace shit */
