#pragma once

#define POSIX 0b1
#define WIN32 0b10
#define COSMO 0b100

#if defined __linux__ || defined BSD || defined __APPLE__ || __COSMOPOLITAN__
#include <unistd.h>
#define SHIT_SUPPORT_VECTOR (POSIX | COSMO)
#elif defined _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define SHIT_SUPPORT_VECTOR (WIN32 | COSMO)
#endif

#define OS_IS(os) (SHIT_SUPPORT_VECTOR & os)

#if OS_IS(WIN32)
using SHIT_FD = HANDLE;
#define SHIT_FD_INVALID INVALID_HANDLE_VALUE

#define SHIT_STDIN  GetStdHandle(STD_OUTPUT_HANDLE)
#define SHIT_STDOUT GetStdHandle(STD_OUTPUT_HANDLE)
#elif OS_IS(POSIX)
using SHIT_FD = i32;
#define SHIT_SUPPORT_VECTOR POSIX

#define SHIT_STDIN  STDIN_FILENO
#define SHIT_STDOUT STDOUT_FILENO
#endif
