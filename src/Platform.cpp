#include "Platform.hpp"

#if SHIT_PLATFORM_IS POSIX
/* clang-format off */
#include "PlatformPosixExtra.cpp"
#include "PlatformPosix.cpp"
/* clang-format on */
#elif SHIT_PLATFORM_IS WIN32
#include "PlatformWin32.cpp"
#else
#error Unsupported platform
#endif
