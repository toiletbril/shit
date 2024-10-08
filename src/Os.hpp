#pragma once

#define POSIX 0b1
#define WIN32 0b10
#define COSMO 0b100

/* clang-format off */
/* Currently, Linux, Windows and Cosmopolitan builds are supported. */
#if defined __linux__ || defined BSD || defined __APPLE__ ||                   \
    defined __COSMOPOLITAN__
#include <cerrno>
#include <fcntl.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined __COSMOPOLITAN__
#include <cosmo.h>
#define SHIT_SUPPORT_VECTOR (COSMO | POSIX)
#else
#define SHIT_SUPPORT_VECTOR (POSIX)
#endif
#elif defined _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define SHIT_SUPPORT_VECTOR (WIN32)
#endif
/* clang-format on */

#define PLATFORM_IS(os) (SHIT_SUPPORT_VECTOR & os)

#include <string>
#include <vector>

namespace shit {

namespace os {

#if PLATFORM_IS(WIN32)
constexpr char PATH_DELIMITER = ';';

using process = HANDLE;
using descriptor = HANDLE;
using os_args = std::string;

#define SHIT_INVALID_FD      INVALID_HANDLE_VALUE
#define SHIT_INVALID_PROCESS INVALID_HANDLE_VALUE

#define SHIT_STDIN  GetStdHandle(STD_INPUT_HANDLE)
#define SHIT_STDOUT GetStdHandle(STD_OUTPUT_HANDLE)

#elif PLATFORM_IS(POSIX)
constexpr char PATH_DELIMITER = ':';

using process = pid_t;
using descriptor = int;
using os_args = std::vector<const char *>;

#define SHIT_INVALID_FD      -1
#define SHIT_INVALID_PROCESS -1

#define SHIT_STDIN  STDIN_FILENO
#define SHIT_STDOUT STDOUT_FILENO
#endif

} /* namespace os */

} /* namespace shit */
