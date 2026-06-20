#pragma once

#include "Common.hpp"

namespace shit {

/* The mode a mimicked script runs in, chosen from its shebang. A sh or dash
   shebang gives Posix, a bash shebang gives Bash, and a shit shebang gives
   Default. */
enum class mimic_mood : u8
{
  Default,
  Posix,
  Bash,
};

} // namespace shit
