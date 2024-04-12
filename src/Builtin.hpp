#pragma once

#include "Common.hpp"

#include <string>
#include <unordered_map>
#include <vector>

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
  virtual i32  execute() const = 0;
  usize        location() const;

protected:
  Builtin(usize location, std::vector<std::string> args);

  usize                    m_location;
  std::vector<std::string> m_args;
};

const std::unordered_map<std::string, Builtin::Kind> BUILTINS = {
    {"echo", Builtin::Kind::Echo},
    {"exit", Builtin::Kind::Exit},
    {"cd",   Builtin::Kind::Cd  },
};

struct Echo : public Builtin
{
  Echo(usize location, std::vector<std::string> args);

  Kind kind() const override;
  i32  execute() const override;
};

struct Cd : public Builtin
{
  Cd(usize location, std::vector<std::string> args);

  Kind kind() const override;
  i32  execute() const override;
};

struct Exit : public Builtin
{
  Exit(usize location, std::vector<std::string> args);

  Kind kind() const override;
  i32  execute() const override;
};

Builtin::Kind shit_search_builtin(std::string_view builtin_name);

i32 shit_exec_builtin(usize location, Builtin::Kind kind,
                      const std::vector<std::string> &args);
