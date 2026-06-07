#include "Common.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace {

#include "toiletline/toiletline.h"

} /* namespace */

namespace shit {
class EvalContext;
} /* namespace shit */

namespace toiletline {

using shit::String;
using shit::StringView;

void set_title(const String &title);

/* Register the shell completion engine as the line editor's TAB and ghost-text
   source, reading names and the filesystem through the given context. Called
   only on the interactive path, so a non-interactive run never enables it.
   disable_completion clears the callback. */
void enable_completion(shit::EvalContext &context);
void disable_completion();

usize utf8_strlen(const String &s, usize byte_count = static_cast<usize>(-1));

/* The code point count of the first byte_count bytes of a raw buffer. This
   counts in place over the caller's bytes, so a located error formats its
   column without copying the whole source per call. */
usize utf8_strnlen(const char *bytes, usize byte_count);

bool is_active();

void initialize();

void exit();

/* The outcome of one line read. The code is the toiletline status, such as
   enter, end of file, or an interrupt, and text is the bytes the user typed. */
struct input_result
{
  i32 code;
  String text;
};

input_result get_input(const String &prompt);

void set_input(const String &input);

void enter_raw_mode();

void exit_raw_mode();

void emit_newlines(StringView buffer);

} /* namespace toiletline */
