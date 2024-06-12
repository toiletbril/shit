#pragma once

#define POSIX 0b1
#define WIN32 0b10
#define COSMO 0b100

/* Currently, Linux, Windows and Cosmopolitan builds are supported. */
#if defined __linux__ || defined BSD || defined __APPLE__ || __COSMOPOLITAN__
#include <unistd.h>
#define SHIT_SUPPORT_VECTOR (POSIX | COSMO)
#elif defined _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define SHIT_SUPPORT_VECTOR (WIN32 | COSMO)
#endif

#define OS_IS(os) (SHIT_SUPPORT_VECTOR & os)

namespace shit {

namespace os {

#if OS_IS(WIN32)
/* Windows handles. */
using descriptor = HANDLE;
#define SHIT_INVALID_FD INVALID_HANDLE_VALUE

/* Universal macros for current STDIN/STDOUT. */
#define SHIT_STDIN  GetStdHandle(STD_OUTPUT_HANDLE)
#define SHIT_STDOUT GetStdHandle(STD_OUTPUT_HANDLE)
#elif OS_IS(POSIX)
/* POSIX descriptors. */
using descriptor = int;
#define SHIT_INVALID_FD -1

#define SHIT_STDIN  STDIN_FILENO
#define SHIT_STDOUT STDOUT_FILENO
#endif

} // namespace os

} /* namespace shit */
