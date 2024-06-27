#pragma once

#include "Common.hpp"
#include "Os.hpp"

#include <optional>
#include <string>

namespace shit {

namespace utils {
struct ExecContext;
}

struct Builtin
{
  enum class Kind : uint8_t
  {
    Echo,
    Cd,
    Exit,
  };

  void set_fds(os::descriptor in, os::descriptor out);
  void print_to_stdout(const std::string &s) const;

  virtual Kind kind() const = 0;
  virtual i32  execute(utils::ExecContext &ec) const = 0;

  virtual ~Builtin() = default;

protected:
  Builtin();
};

struct Echo : public Builtin
{
  Echo();

  Kind kind() const override;
  i32  execute(utils::ExecContext &ec) const override;
};

struct Cd : public Builtin
{
  Cd();

  Kind kind() const override;
  i32  execute(utils::ExecContext &ec) const override;
};

struct Exit : public Builtin
{
  Exit();

  Kind             kind() const override;
  [[noreturn]] i32 execute(utils::ExecContext &ec) const override;
};

std::optional<Builtin::Kind> search_builtin(std::string_view builtin_name);

i32 execute_builtin(utils::ExecContext &&ec);

} /* namespace shit */
