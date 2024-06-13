#include "Builtin.hpp"

#include "Debug.hpp"
#include "Errors.hpp"
#include "Os.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Utils.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

#if !defined _WIN32
#include <unistd.h>
#else
#include <io.h>
#define write _write
#endif

namespace shit {

static const std::unordered_map<std::string, Builtin::Kind> BUILTINS = {
    {"echo", Builtin::Kind::Echo},
    {"exit", Builtin::Kind::Exit},
    {"cd",   Builtin::Kind::Cd  },
};

std::optional<Builtin::Kind>
search_builtin(std::string_view builtin_name)
{
  std::string lower_builtin_name;
  for (char c : builtin_name) {
    lower_builtin_name += std::tolower(c);
  }

  if (auto b = BUILTINS.find(lower_builtin_name.c_str()); b != BUILTINS.end()) {
    return b->second;
  }

  return std::nullopt;
}

i32
execute_builtin(utils::ExecContext &&ec)
{
  std::unique_ptr<Builtin> b;

  switch (ec.builtin_kind()) {
    /* clang-format off */
  case Builtin::Kind::Echo: b.reset(new Echo); break;
  case Builtin::Kind::Cd:   b.reset(new Cd); break;
  case Builtin::Kind::Exit: b.reset(new Exit); break;
    /* clang-format on */

  default:
    SHIT_UNREACHABLE("Unhandled builtin of kind %d", E(ec.builtin_kind()));
  }

  b->set_fds(ec.in.value_or(SHIT_STDIN), ec.out.value_or(SHIT_STDOUT));

  try {
    /* Close FDs as child processes do. */
    i32 ret = b->execute(ec.args());
    ec.close_fds();
    return ret;
  } catch (Error &err) {
    throw ErrorWithLocation{ec.location(), err.message()};
  }
}

/**
 * class: Builtin
 */
Builtin::Builtin() = default;

void
Builtin::set_fds(os::descriptor in, os::descriptor out)
{
  in_fd = in;
  out_fd = out;
}

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
  buf += '\n';

  os::write_fd(out_fd, buf.data(), buf.size());

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
