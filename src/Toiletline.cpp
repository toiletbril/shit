/* Toiletline.hpp is not included to define toiletline configuration macros here
 * rather than in the header. */

#include "Arena.hpp"
#include "Cli.hpp"
#include "Colors.hpp"
#include "Completion.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Path.hpp"
#include "Platform.hpp"
#include "Utils.hpp"

#include <cstdlib>
#include <cstring>

#if !defined(SHIT_NO_TOILETLINE)

namespace {

/* The line editor allocates its history and line buffers through these hooks,
   which draw from one arena that lives for the whole interactive session, since
   the history persists across lines. A non-interactive run never starts the
   editor, so it pays None. The bump arena cannot resize or free a single
   block, so a free or a grown realloc returns the old block to a free list that
   the next allocation reuses. That bounds memory to the working set rather than
   growing it on every keystroke. A header before each block records the block's
   capacity, used both to copy the right bytes on realloc and to size a reused
   block. Each free block links to the next through its own payload. */
shit::BumpArena TOILETLINE_ARENA{};

constexpr usize TL_ALLOC_HEADER = 16;

struct tl_free_block
{
  tl_free_block *next;
};
tl_free_block *TOILETLINE_FREE_LIST = nullptr;

fn tl_block_capacity(void *payload) -> usize &
{
  return *reinterpret_cast<usize *>(static_cast<char *>(payload) -
                                    TL_ALLOC_HEADER);
}

fn tl_arena_malloc(usize length) -> void *
{
  /* Reuse the first free block large enough, so repeated same-size line-buffer
     allocations do not keep growing the arena. */
  for (tl_free_block **link = &TOILETLINE_FREE_LIST; *link != nullptr;
       link = &(*link)->next)
  {
    void *payload = *link;
    if (tl_block_capacity(payload) >= length) {
      *link = (*link)->next;
      return payload;
    }
  }

  char *base = static_cast<char *>(
      TOILETLINE_ARENA.allocate(length + TL_ALLOC_HEADER, TL_ALLOC_HEADER));
  *reinterpret_cast<usize *>(base) = length;
  return base + TL_ALLOC_HEADER;
}

fn tl_arena_free(void *pointer) -> void
{
  if (pointer == nullptr) return;
  tl_free_block *block = static_cast<tl_free_block *>(pointer);
  block->next = TOILETLINE_FREE_LIST;
  TOILETLINE_FREE_LIST = block;
}

fn tl_arena_realloc(void *pointer, usize length) -> void *
{
  if (pointer == nullptr) return tl_arena_malloc(length);
  usize old_capacity = tl_block_capacity(pointer);
  if (old_capacity >= length) return pointer;
  void *fresh = tl_arena_malloc(length);
  std::memcpy(fresh, pointer, old_capacity);
  tl_arena_free(pointer);
  return fresh;
}

#define TL_MALLOC  tl_arena_malloc
#define TL_REALLOC tl_arena_realloc
#define TL_FREE    tl_arena_free
#define TL_ABORT() std::abort()

#define TL_NO_SUSPEND
#define TL_ASSERT           ASSERT
#define TL_HISTORY_MAX_SIZE 1024 * 4

#define TOILETLINE_IMPLEMENTATION
/* A release build makes TL_ASSERT a no-op, which leaves a few of the vendored
   helpers unused. The dependency is not ours to edit, so the warning is
   silenced only around its include. */
#if defined __clang__ || defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "toiletline/toiletline.h"
#if defined __clang__ || defined __GNUC__
#pragma GCC diagnostic pop
#endif

/* The context the completion engine reads, set by the host before each line is
   read and cleared when completion is disabled. A null context means no
   completion, the non-interactive default. */
shit::EvalContext *COMPLETION_CONTEXT = nullptr;

/* The storage the completion callback hands back to toiletline. The C-string
   pointers must outlive the callback return, so the engine's owned strings are
   kept here until the next call replaces them. */
shit::ArrayList<shit::String> COMPLETION_CANDIDATES{};
shit::ArrayList<const char *> COMPLETION_CANDIDATE_POINTERS{};
shit::String COMPLETION_LCP{};

/* The byte offset of the codepoint at the given codepoint index in a UTF-8
   buffer, walking from the start and counting the lead bytes. An index at or
   past the codepoint count returns the byte length, so a clamped cursor maps to
   the end of the buffer. */
fn byte_offset_of_codepoint(const char *bytes, usize byte_length,
                            usize codepoint_index) -> usize
{
  usize byte_offset = 0;
  usize seen_codepoints = 0;
  while (byte_offset < byte_length && seen_codepoints < codepoint_index) {
    /* A byte that is not a UTF-8 continuation byte starts a codepoint. */
    if ((static_cast<unsigned char>(bytes[byte_offset]) & 0xC0) != 0x80)
      seen_codepoints += 1;
    byte_offset += 1;
  }
  /* A continuation byte that trails the last counted codepoint is not the start
     of the next one, so step over the rest of that codepoint. */
  while (byte_offset < byte_length &&
         (static_cast<unsigned char>(bytes[byte_offset]) & 0xC0) == 0x80)
    byte_offset += 1;
  return byte_offset;
}

/* Bridge toiletline's TAB and ghost queries onto the shell completion engine.
   The engine completes the token under the cursor and the result is stored in
   the static buffers above, which toiletline reads right after this returns.

   Toiletline edits in codepoints, so the cursor arrives as a codepoint index
   and the token bounds must go back as codepoint indices, while the completion
   engine works in bytes. The cursor is converted to a byte offset before the
   engine runs, and the byte token bounds it returns are converted back to
   codepoint indices here. Returns 1 when at least one candidate was produced, 0
   otherwise. */
fn shit_completion_callback(const char *buffer, size_t cursor,
                            tl_completion *out) -> int
{
  if (COMPLETION_CONTEXT == nullptr) return 0;

  /* Toiletline calls this through a C function pointer, so a C++ exception that
     unwound past this frame would tear through C stack frames, which is
     undefined behavior. The whole body is guarded and any throw is swallowed
     into a no-completion result that leaves the line unchanged. */
  try {
    const usize byte_length = std::strlen(buffer);
    shit::StringView line{buffer, byte_length};
    shit::Path base = shit::Path::current_directory();

    const usize byte_cursor =
        byte_offset_of_codepoint(buffer, byte_length, cursor);

    shit::completion::completion_result result = shit::completion::complete(
        line, byte_cursor, *COMPLETION_CONTEXT, base);

    if (result.candidates.is_empty()) return 0;

    COMPLETION_CANDIDATES = steal(result.candidates);
    COMPLETION_LCP = steal(result.longest_common_prefix);

    COMPLETION_CANDIDATE_POINTERS.clear();
    for (const shit::String &candidate : COMPLETION_CANDIDATES)
      COMPLETION_CANDIDATE_POINTERS.push(candidate.c_str());

    out->candidates = COMPLETION_CANDIDATE_POINTERS.begin();
    out->count = COMPLETION_CANDIDATE_POINTERS.count();
    out->longest_common_prefix = COMPLETION_LCP.c_str();
    /* The engine reports the token span in bytes, so convert each boundary to a
       codepoint index for toiletline, which replaces the span in codepoints. */
    out->token_start = ::tl_utf8_strnlen(buffer, result.token_start);
    out->token_end = ::tl_utf8_strnlen(buffer, result.token_end);

    return 1;
  } catch (...) {
    return 0;
  }
}

/* Bridge toiletline's render-time highlight query onto the shell. The shell
   colors the whole line by token, a command word by whether it resolves, a
   reserved word, a string, an expansion, an operator, and a comment, and
   returns the colored spans.

   Toiletline edits in codepoints and renders the spans in codepoints, while the
   shell scans in bytes, so each byte span is converted back to codepoint
   indices here, the same conversion the completion bridge performs. The
   conversion is monotonic, so the spans stay sorted and non-overlapping.
   Returns 1 when at least one span is filled, 0 otherwise. The body is guarded
   because toiletline calls it through a C function pointer, so a throw must not
   unwind through the C frames. */
fn shit_highlight_callback(const char *buffer, tl_highlight *out) -> int
{
  if (COMPLETION_CONTEXT == nullptr) return 0;

  try {
    const usize byte_length = std::strlen(buffer);
    shit::StringView line{buffer, byte_length};

    shit::ArrayList<shit::completion::highlight_span> result =
        shit::completion::highlight_line(line, *COMPLETION_CONTEXT);

    size_t filled = 0;
    for (const shit::completion::highlight_span &span : result) {
      if (filled >= out->capacity) break;
      out->spans[filled].start = ::tl_utf8_strnlen(buffer, span.start);
      out->spans[filled].end = ::tl_utf8_strnlen(buffer, span.end);
      out->spans[filled].sgr = span.sgr.data;
      filled++;
    }
    out->count = filled;
    return filled > 0 ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

} /* namespace */

namespace toiletline {

using shit::String;
using shit::StringView;

struct input_result
{
  i32 code;
  String text;
};

static char TL_BUFFER[ITL_STRING_MAX_LEN];

static constexpr char SHIT_HISTORY_FILE[] = ".shit_history";

/* The command history file, ~/.shit_history by default. A test or a one-off
   session redirects it through the SHIT_HISTORY environment variable so it does
   not clobber the real history. None when there is no home and no override. */
static fn history_file_path() -> shit::Maybe<shit::Path>
{
  if (let const override_path =
          shit::os::get_environment_variable("SHIT_HISTORY");
      override_path.has_value() && !override_path->is_empty())
  {
    return shit::Path{override_path->view()};
  }
  let home = shit::os::get_home_directory();
  if (!home) return shit::None;
  let path = *home;
  path.push_component(SHIT_HISTORY_FILE);
  return path;
}

fn set_title(const String &title) -> void
{
  if (::tl_set_title(title.c_str()) != TL_SUCCESS)
    throw shit::Error{"Toiletline: Could not set the title for the terminal"};
}

fn enable_completion(shit::EvalContext &context) -> void
{
  COMPLETION_CONTEXT = &context;
  ::tl_set_complete_callback(shit_completion_callback);
  /* The highlighter shares the completion context and the same enable path, so
     the first word is colored only when completion is on, and a non-tty or the
     -T flag leaves the line plain. */
  if (shit::colors::stdout_wants_color())
    ::tl_set_highlight_callback(shit_highlight_callback);
}

fn disable_completion() -> void
{
  COMPLETION_CONTEXT = nullptr;
  ::tl_set_complete_callback(nullptr);
  ::tl_set_highlight_callback(nullptr);
}

fn utf8_strlen(const String &s, usize count) -> usize
{
  return (count != static_cast<usize>(-1)) ? ::tl_utf8_strnlen(s.c_str(), count)
                                           : ::tl_utf8_strlen(s.c_str());
}

fn utf8_strnlen(const char *bytes, usize byte_count) -> usize
{
  return ::tl_utf8_strnlen(bytes, byte_count);
}

fn is_active() -> bool { return ::itl_g_is_active; }

fn initialize() -> void
{
  /* Load history. */
  if (shit::Maybe<shit::Path> shit_history = history_file_path();
      shit_history.has_value())
  {
    if (int ret = ::tl_history_load(shit_history->c_str()); ret != TL_SUCCESS) {
      /* Don't count non-existent history file as an error. */
      if (ret != -ENOENT) {
        shit::String err_message = "Toiletline: Could not load history: ";
        if (errno == EINVAL)
          err_message += "Non-text byte detected in history file. Truncate it "
                         "manually";
        else
          err_message += shit::os::last_system_error_message();
        shit::Error e{err_message};
        shit::show_message(e.to_string());
      }
    }
  }

  if (::tl_init() != TL_SUCCESS) {
    throw shit::Error{"Toiletline: Could not initialize the terminal. If you "
                      "meant use stdin, provide '-' as an argument"};
  }
}

fn exit() -> void
{
  /* Dump history. */
  if (shit::Maybe<shit::Path> shit_history = history_file_path();
      shit_history.has_value())
  {
    if (int ret = ::tl_history_dump(shit_history->c_str());
        ret != TL_SUCCESS && ret != -EINVAL)
    {
      shit::Error e{"Toiletline: Could not dump history: " +
                    shit::os::last_system_error_message()};
      shit::show_message(e.to_string());
    }
  }

  if (::tl_exit() != TL_SUCCESS) {
    throw shit::Error{"Toiletline: Error while exiting"};
  }
}

fn get_input(const String &prompt) -> input_result
{
  i32 code = ::tl_get_input(TL_BUFFER, sizeof(TL_BUFFER), prompt.c_str());
  if (code == TL_ERROR) {
    throw shit::Error{
        "Toiletline: Unexpected internal error while getting the input"};
  }
  return input_result{code, String{TL_BUFFER}};
}

fn set_input(const String &input) -> void
{
  ::tl_set_predefined_input(input.c_str());
}

fn enter_raw_mode() -> void
{
  if (::tl_enter_raw_mode() != TL_SUCCESS)
    throw shit::Error{"Toiletline: Couldn't force the terminal into raw mode"};
}

fn exit_raw_mode() -> void
{
  if (::tl_exit_raw_mode() != TL_SUCCESS) {
    throw shit::Error{
        "Toiletline: Couldn't force the terminal to exit raw mode"};
  }
}

fn emit_newlines(StringView buffer) -> void
{
  if (::tl_emit_newlines(buffer.data) != TL_SUCCESS)
    throw shit::Error{"Toiletline: Couldn't emit newlines"};
}

} /* namespace toiletline */

#else /* SHIT_NO_TOILETLINE */

/* The line editor is compiled out, so interactive input is unavailable. These
   stubs keep the rest of the shell linking for profiling and debugging, where
   the raw terminal handling would otherwise perturb the run. */
namespace toiletline {

using shit::String;
using shit::StringView;

struct input_result
{
  i32 code;
  String text;
};

fn set_title(const String &title) -> void { unused(title); }

fn enable_completion(shit::EvalContext &context) -> void { unused(context); }

fn disable_completion() -> void {}

fn utf8_strlen(const String &s, usize count) -> usize
{
  return (count != static_cast<usize>(-1) && count < s.length()) ? count
                                                                 : s.length();
}

fn utf8_strnlen(const char *bytes, usize byte_count) -> usize
{
  unused(bytes);
  return byte_count;
}

fn is_active() -> bool { return false; }

fn initialize() -> void
{
  throw shit::Error{
      "This build has no line editor, use '-c', '-s', or a file argument"};
}

fn exit() -> void {}

fn get_input(const String &prompt) -> input_result
{
  unused(prompt);
  throw shit::Error{"This build has no line editor"};
}

fn set_input(const String &input) -> void { unused(input); }

fn enter_raw_mode() -> void {}

fn exit_raw_mode() -> void {}

fn emit_newlines(StringView buffer) -> void { unused(buffer); }

} /* namespace toiletline */

#endif /* SHIT_NO_TOILETLINE */
