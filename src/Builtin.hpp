#pragma once

#include "Common.hpp"

#include <string>
#include <vector>

namespace shit {

struct Builtin
{
  enum class Kind : uint8_t
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
