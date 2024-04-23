#include "Common.hpp"
#include "Debug.hpp"

#include <string>
#include <tuple>

namespace {

#define TL_ASSERT SHIT_ASSERT
#include "toiletline/toiletline.h"

} // namespace

namespace toiletline {

bool is_active();

void initialize();

void exit();

std::tuple<i32, std::string> readline(usize            max_buffer_size,
                                      std::string_view prompt);

void enter_raw_mode();

void exit_raw_mode();

} /* namespace toiletline */
