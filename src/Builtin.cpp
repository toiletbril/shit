#include "Builtin.hpp"

#include "Common.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
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
execute_builtin(const utils::ExecContext &ec)
{
  Builtin::Kind kind = std::get<Builtin::Kind>(ec.kind);

  std::unique_ptr<Builtin> b;

  switch (kind) {
    /* clang-format off */
  case Builtin::Kind::Echo: b.reset(new Echo); break;
  case Builtin::Kind::Cd:   b.reset(new Cd); break;
  case Builtin::Kind::Exit: b.reset(new Exit); break;
    /* clang-format on */

  default: break;
  }

  b->set_fds(ec.in.value_or(SHIT_STDIN), ec.out.value_or(SHIT_STDOUT));

  try {
    i32 ret = b->execute(utils::simple_shell_expand_args(ec.args));
    if (ec.in)
      CloseHandle(*ec.in);
    if (ec.out)
      CloseHandle(*ec.out);
    return ret;
  } catch (Error &err) {
    throw ErrorWithLocation{ec.location, err.message()};
  }
}

/**
 * class: Builtin
 */
Builtin::Builtin() = default;

void
Builtin::set_fds(SHIT_FD in, SHIT_FD out)
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

  utils::write_fd(out_fd, buf.data(), buf.size());

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
