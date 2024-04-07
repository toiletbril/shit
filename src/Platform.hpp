#include "Common.hpp"
#include "Errors.hpp"
#include "string"
#include "string_view"
#include "vector"

i32 platform_exec(usize location, const std::string &path,
                  const std::vector<std::string> &args);

bool platform_we_are_child();
