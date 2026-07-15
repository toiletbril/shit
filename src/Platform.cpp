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

namespace shit {
namespace os {

fn read_fd_to_string(os::descriptor fd, Allocator allocator) throws
    -> Maybe<String>
{
  let contents = String{allocator};
  char buffer[16384];
  loop
  {
    let const read_count = read_fd(fd, buffer, sizeof(buffer));
    if (!read_count.has_value()) return None;
    if (*read_count == 0) return contents;
    contents.append(StringView{buffer, *read_count});
  }
}

} /* namespace os */
} /* namespace shit */
