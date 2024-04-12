#include "Builtin.hpp"

#include "Common.hpp"
#include "Debug.hpp"
#include "Utils.hpp"

#include <filesystem>
#include <iostream>
#include <string>

Builtin::Kind
shit_search_builtin(std::string_view builtin_name)
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
shit_exec_builtin(usize location, Builtin::Kind kind,
                  const std::vector<std::string> &args)
{
  INSIST(kind != Builtin::Kind::Invalid);

  switch (kind) {
  case Builtin::Kind::Echo: return Echo{location, args}.execute();
  case Builtin::Kind::Cd: return Cd{location, args}.execute();
  case Builtin::Kind::Exit: return Exit{location, args}.execute();

  default: UNREACHABLE("Unhandled builtin of type %d", kind);
  }
}

/**
 * class: Builtin
 */
Builtin::Builtin(usize location, std::vector<std::string> args)
    : m_location(location), m_args(args)
{}

/**
 * class: Echo
 */
Echo::Echo(usize location, std::vector<std::string> args)
    : Builtin(location, args)
{}

Builtin::Kind
Echo::kind() const
{
  return Kind::Echo;
}

i32
Echo::execute() const
{
  std::string buf;

  if (m_args.size() > 0) {
    buf += m_args[0];
    for (usize i = 1; i < m_args.size(); i++) {
      buf += ' ';
      buf += m_args[i];
    }
  }

  std::cout << buf << std::endl;
  return 0;
}

/**
 * class: Cd
 */
Cd::Cd(usize location, std::vector<std::string> args) : Builtin(location, args)
{}

Builtin::Kind
Cd::kind() const
{
  return Kind::Cd;
}

i32
Cd::execute() const
{
  std::string arg_path;

  if (m_args.size() > 0) {
    arg_path += m_args[0];
    for (usize i = 1; i < m_args.size(); i++) {
      arg_path += ' ';
      arg_path += m_args[i];
    }
  } else {
    /* Empty cd should go to the parent directory. */
    arg_path = "..";
  }

  if (arg_path.find_first_of('/') != std::string::npos) {
    std::filesystem::path               current_dir = shit_current_directory();
    std::filesystem::directory_iterator d{current_dir};

    for (const std::filesystem::directory_entry &f : d) {
      if (f.is_directory() && f.path().filename() == arg_path) {
        std::filesystem::path np = current_dir / arg_path;

        if (std::filesystem::exists(np)) {
          shit_current_directory_set(np);
          return 0;
        }

        break;
      }
    }
  } else {
    std::filesystem::path np = arg_path;

    if (std::filesystem::exists(np)) {
      shit_current_directory_set(np);
      return 0;
    }
  }

  throw ErrorWithLocation{m_location, "Path '" + arg_path + "' does not exist"};
}

/**
 * class: Exit
 */
Exit::Exit(usize location, std::vector<std::string> args)
    : Builtin(location, args)
{}

Builtin::Kind
Exit::kind() const
{
  return Kind::Exit;
}

i32
Exit::execute() const
{
  i32 ret = (m_args.size() > 0) ? std::atoi(m_args[0].c_str()) : 0;
  shit_exit(ret);
  UNREACHABLE();
}
