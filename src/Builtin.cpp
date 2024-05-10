#include "Builtin.hpp"

#include "Common.hpp"
#include "Debug.hpp"
#include "Toiletline.hpp"
#include "Utils.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace shit {

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
  case Builtin::Kind::Echo: b.reset(new Echo); break;
  case Builtin::Kind::Cd: b.reset(new Cd); break;
  case Builtin::Kind::Exit: b.reset(new Exit); break;

  default: SHIT_UNREACHABLE("Unhandled builtin of type %d", kind);
  }

  return b->execute(args);
}

/**
 * class: Builtin
 */
Builtin::Builtin() {}

/**
 * class: Echo
 */
Echo::Echo() {}

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
Cd::Cd() {}

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
Exit::Exit() {}

Builtin::Kind
Exit::kind() const
{
  return Kind::Exit;
}

i32
Exit::execute(const std::vector<std::string> &args) const
{
  utils::quit(args.size() > 0 ? std::atoi(args[0].c_str()) : 0);
}

} /* namespace shit */
