#include "Platform.hpp"

#if defined __linux__
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Only shell pid can execute some operations. */
static const pid_t SHELL_PID = getpid();

i32
platform_exec(usize location, const std::string &path,
              const std::vector<std::string> &args)
{
  std::vector<const char *> real_args;

  /* argv[0] is the program path itself. */
  real_args.push_back(path.c_str());
  /* Then actual arguments. */
  for (const std::string &arg : args) {
    real_args.push_back(arg.c_str());
  }
  /* And then NULL at the end. */
  real_args.push_back(nullptr);

  i32   status = 0;
  pid_t pid = fork();

  if (pid == -1) {
    throw ErrorWithLocation(location,
                            "fork() failed: " + std::string{strerror(errno)});
  } else if (pid == 0) {
    if (execvp(path.c_str(), const_cast<char *const *>(real_args.data())) != 0)
    {
      throw ErrorWithLocation(location, std::string{strerror(errno)});
    }
  } else {
    if (waitpid(pid, &status, 0) == -1) {
      throw ErrorWithLocation(location, "waitpid() failed: " +
                                            std::string{strerror(errno)});
    }
    return status;
  }

  UNREACHABLE();
}

bool
platform_we_are_child()
{
  return getpid() != SHELL_PID;
}
#endif /* __linux__ */
