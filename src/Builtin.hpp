#pragma once

#include "Common.hpp"

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

  void set_stdin(int fd);
  void set_stdout(int fd);

  virtual Kind kind() const = 0;
  virtual i32  execute(const std::vector<std::string> &args) const = 0;

  virtual ~Builtin() = default;

protected:
  Builtin();

  i32 in_fd{0};
  i32 out_fd{1};
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
