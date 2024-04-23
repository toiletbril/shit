#pragma once

#include "Common.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace shit {

struct Builtin
{
  enum class Kind
  {
    Invalid,
    Echo,
    Cd,
    Exit,
  };

  virtual Kind kind() const = 0;
  virtual i32  execute(const std::vector<std::string> &args) const = 0;

  virtual ~Builtin() = default;

protected:
  Builtin();
};

const std::unordered_map<std::string, Builtin::Kind> BUILTINS = {
    {"echo", Builtin::Kind::Echo},
    {"exit", Builtin::Kind::Exit},
    {"cd",   Builtin::Kind::Cd  },
};

struct Echo : public Builtin
{
  Echo();

  Kind kind() const override;
  i32  execute(const std::vector<std::string> &args) const override;
};

struct Cd : public Builtin
{
  Cd();

  Kind kind() const override;
  i32  execute(const std::vector<std::string> &args) const override;
};

struct Exit : public Builtin
{
  Exit();

  Kind             kind() const override;
  [[noreturn]] i32 execute(const std::vector<std::string> &args) const override;
};

Builtin::Kind search_builtin(std::string_view builtin_name);

i32 execute_builtin(Builtin::Kind kind, const std::vector<std::string> &args);

} /* namespace shit */
