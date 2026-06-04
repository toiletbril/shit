/* Toiletline.hpp is not included to define toiletline configuration macros here
 * rather than in the header. */

#include "Arena.hpp"
#include "Cli.hpp"
#include "Debug.hpp"
#include "Errors.hpp"
#include "Platform.hpp"
#include "Utils.hpp"

#include <cstdlib>
#include <cstring>
#include <tuple>
#include <vector>

#if !defined(SHIT_NO_TOILETLINE)

namespace {

/* The line editor allocates its history and line buffers through these hooks,
   which draw from one arena that lives for the whole interactive session, since
   the history persists across lines. A non-interactive run never starts the
   editor, so it pays nothing. The bump arena cannot resize or free a single
   block, so a free or a grown realloc returns the old block to a free list that
   the next allocation reuses. That bounds memory to the working set rather than
   growing it on every keystroke. A header before each block records the block's
   capacity, used both to copy the right bytes on realloc and to size a reused
   block. Each free block links to the next through its own payload. */
shit::BumpArena g_interactive_arena{};

constexpr usize TL_ALLOC_HEADER = 16;

struct TlFreeBlock
{
  TlFreeBlock *next;
};
TlFreeBlock *g_tl_free_list = nullptr;

usize &
tl_block_capacity(void *payload)
{
  return *reinterpret_cast<usize *>(static_cast<char *>(payload) -
                                    TL_ALLOC_HEADER);
}

void *
tl_arena_malloc(usize length)
{
  /* Reuse the first free block large enough, so repeated same-size line-buffer
     allocations do not keep growing the arena. */
  for (TlFreeBlock **link = &g_tl_free_list; *link != nullptr;
       link = &(*link)->next)
  {
    void *payload = *link;
    if (tl_block_capacity(payload) >= length) {
      *link = (*link)->next;
      return payload;
    }
  }

  char *base = static_cast<char *>(
      g_interactive_arena.allocate(length + TL_ALLOC_HEADER, TL_ALLOC_HEADER));
  *reinterpret_cast<usize *>(base) = length;
  return base + TL_ALLOC_HEADER;
}

void
tl_arena_free(void *pointer)
{
  if (pointer == NULL) return;
  TlFreeBlock *block = static_cast<TlFreeBlock *>(pointer);
  block->next = g_tl_free_list;
  g_tl_free_list = block;
}

void *
tl_arena_realloc(void *pointer, usize length)
{
  if (pointer == NULL) return tl_arena_malloc(length);
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
#define TL_ASSERT           SHIT_ASSERT
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

} /* namespace */

namespace toiletline {

static char TL_BUFFER[ITL_STRING_MAX_LEN];

static constexpr char SHIT_HISTORY_FILE[] = ".shit_history";

void
set_title(const std::string &title)
{
  if (::tl_set_title(title.c_str()) != TL_SUCCESS)
    throw shit::Error{"Toiletline: Could not set the title for the terminal"};
}

usize
utf8_strlen(const std::string &s, usize count)
{
  return (count != std::string::npos) ? ::tl_utf8_strnlen(s.c_str(), count)
                                      : ::tl_utf8_strlen(s.c_str());
}

bool
is_active()
{
  return ::itl_g_is_active;
}

void
initialize()
{
  /* Load history. */
  if (shit::Maybe<std::filesystem::path> h = shit::os::get_home_directory();
      h.has_value())
  {
    std::filesystem::path shit_history = *h / SHIT_HISTORY_FILE;
    if (int ret = ::tl_history_load(shit_history.string().c_str());
        ret != TL_SUCCESS)
    {
      /* Don't count non-existent history file as an error. */
      if (ret != -ENOENT) {
        std::string err_message = "Toiletline: Could not load history: ";
        err_message += (errno == EINVAL)
                           ? std::string{"Non-text byte detected in history "
                                         "file. Truncate it manually"}
                           : shit::os::last_system_error_message();
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

void
exit()
{
  /* Dump history. */
  if (shit::Maybe<std::filesystem::path> h = shit::os::get_home_directory();
      h.has_value())
  {
    std::filesystem::path shit_history = *h / SHIT_HISTORY_FILE;
    if (int ret = ::tl_history_dump(shit_history.string().c_str());
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

std::tuple<i32, std::string>
get_input(const std::string &prompt)
{
  i32 code = ::tl_get_input(TL_BUFFER, sizeof(TL_BUFFER), prompt.c_str());
  if (code == TL_ERROR) {
    throw shit::Error{
        "Toiletline: Unexpected internal error while getting the input"};
  }
  return {code, TL_BUFFER};
}

void
set_input(const std::string &input)
{
  ::tl_set_predefined_input(input.c_str());
}

void
enter_raw_mode()
{
  if (::tl_enter_raw_mode() != TL_SUCCESS)
    throw shit::Error{"Toiletline: Couldn't force the terminal into raw mode"};
}

void
exit_raw_mode()
{
  if (::tl_exit_raw_mode() != TL_SUCCESS) {
    throw shit::Error{
        "Toiletline: Couldn't force the terminal to exit raw mode"};
  }
}

void
emit_newlines(std::string_view buffer)
{
  if (::tl_emit_newlines(buffer.data()) != TL_SUCCESS)
    throw shit::Error{"Toiletline: Couldn't emit newlines"};
}

} /* namespace toiletline */

#else /* SHIT_NO_TOILETLINE */

/* The line editor is compiled out, so interactive input is unavailable. These
   stubs keep the rest of the shell linking for profiling and debugging, where
   the raw terminal handling would otherwise perturb the run. */
namespace toiletline {

void
set_title(const std::string &title)
{
  SHIT_UNUSED(title);
}

usize
utf8_strlen(const std::string &s, usize count)
{
  return (count != std::string::npos && count < s.length()) ? count
                                                            : s.length();
}

bool
is_active()
{
  return false;
}

void
initialize()
{
  throw shit::Error{
      "This build has no line editor, use '-c', '-s', or a file argument"};
}

void
exit()
{}

std::tuple<i32, std::string>
get_input(const std::string &prompt)
{
  SHIT_UNUSED(prompt);
  throw shit::Error{"This build has no line editor"};
}

void
set_input(const std::string &input)
{
  SHIT_UNUSED(input);
}

void
enter_raw_mode()
{}

void
exit_raw_mode()
{}

void
emit_newlines(std::string_view buffer)
{
  SHIT_UNUSED(buffer);
}

} /* namespace toiletline */

#endif /* SHIT_NO_TOILETLINE */
