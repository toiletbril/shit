#pragma once

#include "Common.hpp"
#include "Os.hpp"

#include <optional>
#include <string>
#include <vector>

namespace shit {

struct Builtin
{
  enum class Kind : uint8_t
  {
    Echo,
    Cd,
    Exit,
  };

  void set_fds(os::descriptor in, os::descriptor out);

  virtual Kind kind() const = 0;
  virtual i32  execute(const std::vector<std::string> &args) const = 0;

  virtual ~Builtin() = default;

protected:
  Builtin();

  os::descriptor in_fd{SHIT_STDIN};
  os::descriptor out_fd{SHIT_STDOUT};
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

std::optional<Builtin::Kind> search_builtin(std::string_view builtin_name);

namespace utils {
struct ExecContext;
}

i32 execute_builtin(const utils::ExecContext &ec);

} /* namespace shit */
