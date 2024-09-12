#pragma once

#include "Common.hpp"
#include "Os.hpp"

#include <optional>
#include <string>
#include <unordered_map>

namespace shit {

struct ExecContext;

struct Builtin
{
  enum class Kind : uint8_t
  {
    Echo,
    Cd,
    Exit,
    Pwd,
  };

  void set_fds(os::descriptor in, os::descriptor out);
  void print_to_stdout(const std::string &s) const;

  virtual Kind kind() const = 0;
  virtual i32  execute(ExecContext &ec) const = 0;

  virtual ~Builtin() = default;

protected:
  Builtin();
};

const std::unordered_map<std::string, Builtin::Kind> BUILTINS = {
    {"echo", Builtin::Kind::Echo},
    {"exit", Builtin::Kind::Exit},
    {"cd",   Builtin::Kind::Cd  },
    {"pwd",  Builtin::Kind::Pwd },
};

#define B_CASE(btin)                                                           \
  case Builtin::Kind::btin: b.reset(new btin); break

#define BUILTIN_SWITCH_CASES()                                                 \
  B_CASE(Echo);                                                                \
  B_CASE(Cd);                                                                  \
  B_CASE(Exit);                                                                \
  B_CASE(Pwd)

struct Echo : public Builtin
{
  Echo();

  Kind kind() const override;
  i32  execute(ExecContext &ec) const override;
};

struct Cd : public Builtin
{
  Cd();

  Kind kind() const override;
  i32  execute(ExecContext &ec) const override;
};

struct Exit : public Builtin
{
  Exit();

  Kind             kind() const override;
  [[noreturn]] i32 execute(ExecContext &ec) const override;
};

struct Pwd : public Builtin
{
  Pwd();

  Kind kind() const override;
  i32  execute(ExecContext &ec) const override;
};

std::optional<Builtin::Kind> search_builtin(std::string_view builtin_name);

i32 execute_builtin(ExecContext &&ec);

} /* namespace shit */
