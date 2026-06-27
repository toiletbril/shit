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
#include "Trace.hpp"
#include "Utils.hpp"

#if !defined SHIT_NO_TOILETLINE

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

fn tl_block_capacity(opaque *payload) -> usize &
{
  return *reinterpret_cast<usize *>(static_cast<char *>(payload) -
                                    TL_ALLOC_HEADER);
}

fn tl_arena_malloc(usize length) -> opaque *
{
  for (tl_free_block **link = &TOILETLINE_FREE_LIST; *link != nullptr;
       link = &(*link)->next)
  {
    opaque *payload = *link;
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

fn tl_arena_free(opaque *pointer) -> void
{
  if (pointer == nullptr) return;
  tl_free_block *block = static_cast<tl_free_block *>(pointer);
  block->next = TOILETLINE_FREE_LIST;
  TOILETLINE_FREE_LIST = block;
}

fn tl_arena_realloc(opaque *pointer, usize length) -> opaque *
{
  if (pointer == nullptr) return tl_arena_malloc(length);
  usize old_capacity = tl_block_capacity(pointer);
  if (old_capacity >= length) return pointer;
  opaque *fresh = tl_arena_malloc(length);
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
#define TL_HISTORY_MAX_SIZE (1024 * 4)

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

shit::EvalContext *COMPLETION_CONTEXT = nullptr;

/* The storage the completion callback hands back to toiletline. The C-string
   pointers must outlive the callback return, so the engine's owned strings are
   kept here until the next call replaces them. */
shit::ArrayList<shit::String> COMPLETION_CANDIDATES{};
shit::ArrayList<const char *> COMPLETION_CANDIDATE_POINTERS{};
shit::ArrayList<shit::String> COMPLETION_DESCRIPTIONS{};
shit::ArrayList<const char *> COMPLETION_DESCRIPTION_POINTERS{};
shit::String COMPLETION_LCP{};

/* An index at or past the codepoint count returns the byte length, so a clamped
   cursor maps to the end of the buffer. */
fn byte_offset_of_codepoint(const char *bytes, usize byte_length,
                            usize codepoint_index) -> usize
{
  usize byte_offset = 0;
  usize seen_codepoints = 0;
  while (byte_offset < byte_length && seen_codepoints < codepoint_index) {
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

/* Toiletline edits in codepoints, so the cursor arrives as a codepoint index
   and the token bounds must go back as codepoint indices, while the completion
   engine works in bytes. */
fn shit_completion_callback(const char *buffer, size_t cursor,
                            tl_completion *out, int for_listing) -> int
{
  if (COMPLETION_CONTEXT == nullptr) return 0;

  /* Toiletline calls this through a C function pointer, so a C++ exception that
     unwound past this frame would tear through C stack frames, which is
     undefined behavior. The whole body is guarded and any throw is swallowed
     into a no-completion result that leaves the line unchanged. */
  try {
    const usize byte_length = std::strlen(buffer);
    let line = shit::StringView{buffer, byte_length};
    shit::Path base = shit::Path::current_directory();

    const usize byte_cursor =
        byte_offset_of_codepoint(buffer, byte_length, cursor);

    /* A completion run can raise a diagnostic, such as a bash-completion
       function tripping a warning, while the editor sits on the prompt line. It
       is armed to break onto its own line first, then disarmed whether or not
       it printed, so a later command's message is unaffected. */
    shit::arm_message_leading_newline(true);
    shit::completion::completion_result result = shit::completion::complete(
        line, byte_cursor, *COMPLETION_CONTEXT, base, for_listing != 0);
    shit::arm_message_leading_newline(false);

    if (result.candidates.is_empty()) return 0;

    COMPLETION_CANDIDATES = steal(result.candidates);
    COMPLETION_LCP = steal(result.longest_common_prefix);

    COMPLETION_CANDIDATE_POINTERS.clear();
    COMPLETION_CANDIDATE_POINTERS.reserve(COMPLETION_CANDIDATES.count());
    for (let const &candidate : COMPLETION_CANDIDATES)
      COMPLETION_CANDIDATE_POINTERS.push(candidate.c_str());

    /* The candidate text keys the lookup since the engine keyed the map that
       way to survive the sort. The build is skipped when no description was
       produced, so a per-keystroke completion pays nothing for it. */
    COMPLETION_DESCRIPTIONS.clear();
    COMPLETION_DESCRIPTION_POINTERS.clear();
    out->descriptions = nullptr;
    if (result.descriptions.count() > 0) {
      COMPLETION_DESCRIPTIONS.reserve(COMPLETION_CANDIDATES.count());
      for (let const &candidate : COMPLETION_CANDIDATES) {
        if (const shit::String *found_description =
                result.descriptions.find(candidate.view());
            found_description != nullptr)
          COMPLETION_DESCRIPTIONS.push(shit::String{found_description->view()});
        else
          COMPLETION_DESCRIPTIONS.push(shit::String{});
      }
      COMPLETION_DESCRIPTION_POINTERS.reserve(COMPLETION_DESCRIPTIONS.count());
      for (let const &description : COMPLETION_DESCRIPTIONS)
        COMPLETION_DESCRIPTION_POINTERS.push(description.c_str());
      out->descriptions = COMPLETION_DESCRIPTION_POINTERS.begin();
    }

    out->candidates = COMPLETION_CANDIDATE_POINTERS.begin();
    out->count = COMPLETION_CANDIDATE_POINTERS.count();
    out->longest_common_prefix = COMPLETION_LCP.c_str();
    /* The engine reports the token span in bytes, so convert each boundary to a
       codepoint index for toiletline, which replaces the span in codepoints. */
    out->token_start = ::tl_utf8_strnlen(buffer, result.token_start);
    out->token_end = ::tl_utf8_strnlen(buffer, result.token_end);

    return 1;
  } catch (shit::ErrorBase &error) {
    /* A throw skips the disarm above, so it runs here too, since the flag is
       process-wide and would otherwise push the next command's message down. */
    shit::arm_message_leading_newline(false);
    LOG(Debug, "completion swallowed an error: %s", error.message().c_str());
    return 0;
  } catch (...) {
    shit::arm_message_leading_newline(false);
    LOG(Debug, "completion swallowed an unknown throw");
    return 0;
  }
}

/* The conversion from byte spans to codepoint indices is monotonic, so the
   spans stay sorted and non-overlapping. The body is guarded because toiletline
   calls it through a C function pointer, so a throw must not unwind through the
   C frames. */
fn shit_highlight_callback(const char *buffer, tl_highlight *out) -> int
{
  if (COMPLETION_CONTEXT == nullptr) return 0;
  /* NO_COLOR is honored live, so a runtime export NO_COLOR=1 draws the line
     plain on the next keystroke. The callback is registered unconditionally and
     gated here rather than at startup, so the decision follows the environment
     for the whole session. */
  if (!shit::colors::stdout_wants_color()) return 0;

  try {
    const usize byte_length = std::strlen(buffer);
    let line = shit::StringView{buffer, byte_length};

    shit::ArrayList<shit::completion::highlight_span> result =
        shit::completion::highlight_line(line, *COMPLETION_CONTEXT);

    size_t filled = 0;
    for (let const &span : result) {
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

shit::EvalContext *JOB_CONTEXT = nullptr;
shit::String WAKE_NOTIFICATION_STASH{};

/* The two-phase wake hook the editor's wait loop drives for set -b. Phase 0
   asks whether anything must print, reading and clearing the SIGCHLD flag
   and formatting the Done rows with \r\n endings for the raw-mode screen.
   Phase 1 prints the stash after the editor cleared its render block, and
   the editor redraws the prompt below the rows. The split keeps toiletline
   ignorant of jobs and the shell ignorant of render-row geometry. The body
   is guarded since toiletline calls through a C function pointer. */
fn shit_wake_callback(int phase) -> int
{
  try {
    if (phase == 0) {
      if (shit::os::CHILD_STATE_CHANGED == 0) return 0;
      /* The flag clears only when this hook consumes it, so a completion
         that lands before set -b turns notify on still reports once the
         option flips. */
      if (JOB_CONTEXT == nullptr || !JOB_CONTEXT->notify()) return 0;
      shit::os::CHILD_STATE_CHANGED = 0;
      WAKE_NOTIFICATION_STASH =
          JOB_CONTEXT->format_done_job_notifications("\r\n");
      return WAKE_NOTIFICATION_STASH.is_empty() ? 0 : 1;
    }
    shit::print_error(WAKE_NOTIFICATION_STASH.view());
    shit::flush();
    WAKE_NOTIFICATION_STASH.clear();
    return 0;
  } catch (...) {
    return 0;
  }
}

/* Bridge toiletline's ghost history validation onto the shell, the fish
   autosuggestion rule. An entry whose command word no longer resolves is
   rejected so the scan keeps looking for a live one. A throw accepts the
   entry, since a suggestion is better than none when the resolver hiccups. */
fn shit_ghost_validate_callback(const char *entry) -> int
{
  if (COMPLETION_CONTEXT == nullptr) return 1;
  try {
    const usize byte_length = std::strlen(entry);
    return shit::completion::command_word_resolves(
               shit::StringView{entry, byte_length}, *COMPLETION_CONTEXT)
               ? 1
               : 0;
  } catch (...) {
    return 1;
  }
}

} /* namespace */

namespace toiletline {

using shit::EvalContext;
using shit::Maybe;
using shit::Path;
using shit::String;
using shit::StringView;
namespace colors = shit::colors;
namespace os = shit::os;
namespace utils = shit::utils;

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
  if (!home.has_value()) return shit::None;
  let path = home->clone();
  path.push_component(SHIT_HISTORY_FILE);
  return path;
}

static constexpr char SHIT_CALC_HISTORY_FILE[] = ".shit_calc_history";

/* The calc REPL keeps its expression history in ~/.shit_calc_history, apart
   from the shell command history. The SHIT_CALC_HISTORY environment variable
   redirects it for a test or a one-off session. None when there is no home and
   no override. */
static fn calc_history_file_path() -> shit::Maybe<shit::Path>
{
  if (let const override_path =
          shit::os::get_environment_variable("SHIT_CALC_HISTORY");
      override_path.has_value() && !override_path->is_empty())
  {
    return shit::Path{override_path->view()};
  }
  let home = shit::os::get_home_directory();
  if (!home.has_value()) return shit::None;
  let path = home->clone();
  path.push_component(SHIT_CALC_HISTORY_FILE);
  return path;
}

/* The in-memory history is swapped to the calc file on entry and back to the
   shell file on leave, so a recalled calc expression never lands in the shell
   command history and a shell command never appears in the calc REPL. The shell
   history is dumped first so a later reload restores it whole. */
fn enter_calc_history() -> void
{
  if (shit::Maybe<shit::Path> shell = history_file_path(); shell.has_value())
    ::tl_history_dump(shell->c_str());

  if (shit::Maybe<shit::Path> calc = calc_history_file_path(); calc.has_value())
    ::tl_history_load(calc->c_str());
}

fn leave_calc_history() -> void
{
  if (shit::Maybe<shit::Path> calc = calc_history_file_path(); calc.has_value())
    ::tl_history_dump(calc->c_str());

  if (shit::Maybe<shit::Path> shell = history_file_path(); shell.has_value())
    ::tl_history_load(shell->c_str());
}

fn set_title(const String &title) -> void
{
  /* A terminal that rejects the title escape is cosmetic, not an error worth
     surfacing, so a failure is ignored rather than thrown into the run. */
  ::tl_set_title(title.c_str());
}

fn enable_completion(shit::EvalContext &context) -> void
{
  COMPLETION_CONTEXT = &context;
  ::tl_set_complete_callback(shit_completion_callback);
  ::tl_set_highlight_callback(shit_highlight_callback);
  ::tl_set_ghost_validate_callback(shit_ghost_validate_callback);
}

fn disable_completion() -> void
{
  COMPLETION_CONTEXT = nullptr;
  ::tl_set_complete_callback(nullptr);
  ::tl_set_highlight_callback(nullptr);
  ::tl_set_ghost_validate_callback(nullptr);
}

/* True while the shell completion and highlighter are registered, so the calc
   REPL restores them on leave only when they were on, leaving a -T shell
   completion-free. */
fn completion_is_enabled() -> bool { return COMPLETION_CONTEXT != nullptr; }

fn enable_job_notifications(shit::EvalContext &context) -> void
{
  /* Registered whenever the editor runs, even under -T, since set -b is job
     reporting rather than completion. The callback gates itself on the live
     notify option. */
  JOB_CONTEXT = &context;
  ::tl_set_wake_callback(shit_wake_callback);
}

fn set_ghost_enabled(bool enabled) -> void
{
  ::tl_set_ghost_enabled(enabled ? 1 : 0);
}

fn set_highlight_enabled(bool enabled) -> void
{
  ::tl_set_highlight_callback(enabled ? shit_highlight_callback : nullptr);
}

fn set_edit_mode(bool is_vi) -> void
{
  ::tl_set_edit_mode(is_vi ? TL_EDIT_MODE_VI_INSERT : TL_EDIT_MODE_EMACS);
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
  if (shit::Maybe<shit::Path> shit_history = history_file_path();
      shit_history.has_value())
  {
    /* A history that cannot be loaded is cosmetic, a missing file on the first
       run above all, so the failure is ignored rather than printed at startup.
       The session simply starts with no recalled history. */
    ::tl_history_load(shit_history->c_str());
  }

  if (::tl_init() != TL_SUCCESS) {
    throw shit::Error{"Toiletline: Could not initialize the terminal. If you "
                      "meant use stdin, provide '-' as an argument"};
  }
}

fn exit() -> void
{
  if (shit::Maybe<shit::Path> shit_history = history_file_path();
      shit_history.has_value())
  {
    if (int dump_status = ::tl_history_dump(shit_history->c_str());
        dump_status != TL_SUCCESS && dump_status != -EINVAL)
    {
      shit::Error error{"Toiletline: Could not dump history: " +
                        shit::os::last_system_error_message()};
      shit::show_message(error.to_string());
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
  if (::tl_enter_raw_mode() == TL_SUCCESS) return;
  /* A script run can leave fd 0 pointing away from the terminal, the
     configure-style exec redirection performed in-process, so the controlling
     tty is reopened onto fd 0 and raw mode retried before the prompt gives
     up on the session. */
  if (shit::os::reopen_terminal_as_stdin() &&
      ::tl_enter_raw_mode() == TL_SUCCESS)
  {
    return;
  }
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

static constexpr usize PROMPT_PWD_LENGTH = 24;

static fn shorten_path_with_ellipsis(StringView path, usize max_length) throws
    -> String
{
  if (path.length <= max_length) return String{path};
  /* The ellipsis is three bytes, so a max below it cannot leave a tail and the
     whole path is returned rather than read past its end. */
  if (max_length < 3) return String{path};
  /* The byte cut can land in the middle of a multibyte codepoint, so it
     advances to the next codepoint boundary, the first byte that is not a UTF-8
     continuation byte, before the tail is taken. */
  usize tail_start = path.length - max_length + 3;
  while (tail_start < path.length &&
         (static_cast<unsigned char>(path[tail_start]) & 0xC0) == 0x80)
    tail_start++;
  let shortened = String{};
  shortened += "...";
  shortened += StringView{path.data + tail_start, path.length - tail_start};
  return shortened;
}

static fn git_branch() throws -> String { return utils::current_git_branch(); }

static fn format_prompt_duration(u64 nanos) throws -> String
{
  const u64 milliseconds = nanos / 1000000ULL;
  if (milliseconds < 5) return String{};
  let out = String{};
  if (milliseconds < 1000) {
    out.append(utils::int_to_text(static_cast<i64>(milliseconds)));
    out += "ms";
    return out;
  }
  const u64 tenths = nanos / 100000000ULL;
  out.append(utils::int_to_text(static_cast<i64>(tenths / 10)));
  out += '.';
  out.append(utils::int_to_text(static_cast<i64>(tenths % 10)));
  out += 's';
  return out;
}

/* The current local time formatted with strftime into a fixed buffer, used by
   the clock prompt escapes. localtime is read on the single interactive thread,
   so the shared static tm it returns is not a race. */
static fn prompt_strftime(const char *format) throws -> String
{
  std::time_t now = std::time(nullptr);
  std::tm *local = std::localtime(&now);
  if (local == nullptr) return String{};
  char buffer[128];
  usize written = std::strftime(buffer, sizeof(buffer), format, local);
  return String{
      StringView{buffer, written}
  };
}

static fn prompt_hostname(bool need_full) throws -> String
{
  String host = os::get_hostname().value_or(
      os::get_environment_variable("HOSTNAME").value_or("localhost"));
  if (need_full) return host;
  let const dot = host.view().find_character('.');
  return String{
      host.view().substring_of_length(0, dot.value_or(host.length()))};
}

/* Collapse a leading home directory to ~ the way bash does, only when the home
   prefix ends on a path boundary so HOME=/home/sd and cwd=/home/sderp keeps the
   full path rather than rendering ~erp. */
static fn collapse_home_prefix(StringView path) throws -> String
{
  let shown = String{path};
  Maybe<Path> home = os::get_home_directory();
  if (!home.has_value()) return shown;

  let const home_length = home->count();
  if (shown.starts_with(home->text()) &&
      (shown.length() == home_length || shown.view()[home_length] == '/'))
  {
    let collapsed = String{};
    collapsed += "~";
    collapsed += shown.substring(home_length);
    shown = steal(collapsed);
  }
  return shown;
}

static fn expand_prompt_escapes(StringView prompt, StringView user,
                                StringView working_directory,
                                EvalContext &context) throws -> String
{
  let out = String{};
  for (usize i = 0; i < prompt.length; i++) {
    if (prompt[i] != '\\' || i + 1 >= prompt.length) {
      out += prompt[i];
      continue;
    }
    u8 escaped = static_cast<u8>(prompt[i + 1]);

    if (escaped >= '0' && escaped <= '7') {
      u32 value = 0;
      usize digits = 0;
      while (digits < 3 && i + 1 < prompt.length && prompt[i + 1] >= '0' &&
             prompt[i + 1] <= '7')
      {
        value = value * 8 + static_cast<u32>(prompt[i + 1] - '0');
        i++;
        digits++;
      }
      out += static_cast<char>(value & 0xFF);
      continue;
    }

    i++;
    switch (escaped) {
    case 'u': out += user; break;
    case 'h': out += prompt_hostname(false); break;
    case 'H': out += prompt_hostname(true); break;
    case 'w': out += collapse_home_prefix(working_directory); break;
    case 'W': out += Path{working_directory}.filename(); break;
    case 'P':
      out += shorten_path_with_ellipsis(
          collapse_home_prefix(working_directory).view(), PROMPT_PWD_LENGTH);
      break;
    /* Any wrapping belongs to the PS1 text, the default template spaces it
       through ${SHIT_GIT_BRANCH:+...}. */
    case 'g': out += git_branch(); break;
    case '$': out += (user == "root") ? '#' : '$'; break;
    case 'n': out += '\n'; break;
    case 'r': out += '\r'; break;
    case 'e': out += '\x1b'; break;
    case 'a': out += '\a'; break;
    /* \[ and \] wrap non-printing bytes in bash so the width count skips them.
       The line editor already skips ANSI runs when it measures the prompt, so
       the markers are dropped and the bytes between them are emitted plainly.
     */
    case '[': break;
    case ']': break;
    /* The clock escapes match bash, so \t is the 24-hour time rather than a
       literal tab, and the date and 12-hour forms follow strftime. */
    case 't': out += prompt_strftime("%H:%M:%S"); break;
    case 'T': out += prompt_strftime("%I:%M:%S"); break;
    case '@': out += prompt_strftime("%I:%M %p"); break;
    case 'A': out += prompt_strftime("%H:%M"); break;
    case 'd': out += prompt_strftime("%a %b %d"); break;
    case 's': {
      if (Maybe<String> argv0 = context.get_variable_value("0");
          argv0.has_value())
        out += Path{argv0->view()}.filename();
    } break;
    case 'v':
    case 'V':
      if (Maybe<String> version = context.get_variable_value("BASH_VERSION");
          version.has_value())
        out += *version;
      break;
    case '?': {
      const i32 status = context.last_exit_status();
      const bool should_use_color = colors::stdout_wants_color();
      if (should_use_color)
        out += status == 0 ? colors::ansi::GREEN : colors::ansi::RED;
      out += utils::int_to_text(status);
      if (should_use_color) out += colors::ansi::RESET;
    } break;
    case 'j':
      out += utils::int_to_text(static_cast<i64>(context.jobs().count()));
      break;
    case 'D':
      out += format_prompt_duration(context.last_command_duration_ns());
      break;
    /* \! and \# are the history and command numbers, which this shell does not
       track, so they expand to nothing rather than printing the escape raw. */
    case '!': break;
    case '#': break;
    case '\\': out += '\\'; break;
    default:
      out += '\\';
      out += static_cast<char>(escaped);
      break;
    }
  }
  return out;
}

/* The PS1 expansion from the previous prompt, reusable only while every value
   it read is unchanged. The scanner below collects every name a pure
   parameter-only template references, the cache stores those names with the
   values they had at expansion time, and a later prompt compares each
   name's current value before reusing the result. A template holding a
   command, process, or arithmetic substitution, a funsub, or an assigning
   parameter form has inputs the names cannot capture, so it never caches
   and expands every prompt the way bash expands PS1. */
struct prompt_cache_input
{
  String name{};
  Maybe<String> value{};
};
static String PROMPT_CACHE_TEMPLATE{};
static shit::ArrayList<prompt_cache_input> PROMPT_CACHE_INPUTS{};
static String PROMPT_CACHE_EXPANSION{};
static bool PROMPT_CACHE_VALID = false;

/* Scan the template for parameter references, filling names and reporting
   whether the template is pure enough to cache. The walk is conservative, a
   $ that opens anything other than a plain name or a non-assigning braced
   parameter form marks the template impure. Names inside a braced form's
   word, the :+ alternate, are collected by the same linear walk when their
   own $ comes by. */
static fn scan_prompt_template_inputs(
    StringView text, shit::ArrayList<prompt_cache_input> &names) throws -> bool
{
  let is_name_byte = [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
  };
  let do_add_name = [&](StringView name) throws {
    for (let const &known : names)
      if (known.name.view() == name) return;
    let input = prompt_cache_input{};
    input.name = String{name};
    names.push(steal(input));
  };

  for (usize i = 0; i < text.length; i++) {
    const char byte = text[i];
    if (byte == '`') return false;
    if (byte != '$') continue;
    if (i + 1 >= text.length) continue;
    const char next = text[i + 1];
    if (next == '(') return false;
    if (next == '{') {
      usize j = i + 2;
      /* A brace followed by whitespace or a pipe is the funsub, a command
         with unknowable inputs. */
      if (j < text.length && (text[j] == ' ' || text[j] == '\t' ||
                              text[j] == '\n' || text[j] == '|'))
      {
        return false;
      }
      if (j < text.length && (text[j] == '#' || text[j] == '!')) j++;
      usize name_start = j;
      while (j < text.length && is_name_byte(text[j]))
        j++;
      if (j == name_start) return false;
      do_add_name(text.substring_of_length(name_start, j - name_start));
      /* An = directly or after a colon assigns at expansion time, an input
         the cache cannot key. The other operators only read. */
      if (j < text.length && text[j] == '=') return false;
      if (j + 1 < text.length && text[j] == ':' && text[j + 1] == '=')
        return false;
      i = j - 1;
      continue;
    }
    usize j = i + 1;
    while (j < text.length && is_name_byte(text[j]))
      j++;
    if (j > i + 1) {
      do_add_name(text.substring_of_length(i + 1, j - i - 1));
      i = j - 1;
      continue;
    }
    /* A special parameter such as $? or $$ reads one byte, keyed like a
       name so a status change misses the cache. */
    do_add_name(text.substring_of_length(i + 1, 1));
    i++;
  }
  return true;
}

fn default_prompt_template() -> String
{
  /* The branch renders space-wrapped only when one exists, the bash :+
     alternate around the SHIT_GIT_BRANCH dynamic variable, so the prompt
     closes up to a single space outside a repository. The branch name is
     colored cyan when color is on, and the line editor skips the escape run
     when it measures the prompt width. */
  let template_string = String{};
  const bool should_use_color = colors::stdout_wants_color();

  if (should_use_color) {
    template_string += R"([${SHIT_GIT_BRANCH:+)";
    template_string += colors::ansi::CYAN;
    template_string += R"($SHIT_GIT_BRANCH)";
    template_string += colors::ansi::RESET;
    template_string += R"( at }\u@\h )";
    template_string += colors::ansi::GREEN;
    template_string += R"(\P)";
    template_string += colors::ansi::RESET;
  } else {
    template_string += R"([${SHIT_GIT_BRANCH:+$SHIT_GIT_BRANCH at }\u@\h \P)";
  }
  template_string += R"(] )";
  return template_string;
}

/* The backslash escapes the parameter pass would otherwise unescape, mapped to
   a control-byte marker so they survive expansion and reach the escape pass
   intact. A realistic prompt holds none of these control bytes, so the round
   trip is lossless in practice. A PS1 that embeds a literal 0x01 to 0x03, which
   no normal prompt does, sees that byte rewritten to its escape, an accepted
   cosmetic edge rather than a crash. */
static constexpr char PROMPT_GUARD_DOLLAR = '\x01';
static constexpr char PROMPT_GUARD_BACKSLASH = '\x02';
static constexpr char PROMPT_GUARD_BACKTICK = '\x03';

/* Replace \$, \\, and \` with markers so expand_heredoc_body, which has heredoc
   backslash semantics, does not consume them before the escape pass decodes \$
   and \\. The ${...} and $(...) the user wrote still expand. */
static fn guard_prompt_backslashes(StringView template_string) throws -> String
{
  let out = String{};
  for (usize i = 0; i < template_string.length; i++) {
    if (template_string[i] == '\\' && i + 1 < template_string.length) {
      switch (template_string[i + 1]) {
      case '$':
        out.push(PROMPT_GUARD_DOLLAR);
        i++;
        continue;
      case '\\':
        out.push(PROMPT_GUARD_BACKSLASH);
        i++;
        continue;
      case '`':
        out.push(PROMPT_GUARD_BACKTICK);
        i++;
        continue;
      default: break;
      }
    }
    out.push(template_string[i]);
  }
  return out;
}

/* Restore the guarded escapes after expansion, so the escape pass sees \$ and
   \\ the way the user wrote them. */
static fn unguard_prompt_backslashes(StringView expanded) throws -> String
{
  let out = String{};
  for (usize i = 0; i < expanded.length; i++) {
    switch (expanded[i]) {
    case PROMPT_GUARD_DOLLAR: out += "\\$"; break;
    case PROMPT_GUARD_BACKSLASH: out += "\\\\"; break;
    case PROMPT_GUARD_BACKTICK: out += "\\`"; break;
    default: out.push(expanded[i]); break;
    }
  }
  return out;
}

/* Remove the ANSI SGR color sequences, an escape followed by '[' then the
   parameters then 'm', from the rendered prompt. NO_COLOR is honored live by
   running this on the finished prompt when color is off, so the baked default
   color and a user PS1's color both fall away. A non-color CSI sequence, one
   that ends in a byte other than 'm', is left in place. */
static fn strip_ansi_color(StringView text) throws -> String
{
  let out = String{};
  usize i = 0;
  while (i < text.length) {
    if (text[i] == '\x1b' && i + 1 < text.length && text[i + 1] == '[') {
      usize end = i + 2;
      while (end < text.length && (text[end] < '@' || text[end] > '~'))
        end++;
      if (end < text.length && text[end] == 'm') {
        i = end + 1;
        continue;
      }
    }
    out.push(text[i]);
    i++;
  }
  return out;
}

fn expand_prompt_template(StringView prompt, EvalContext &context) throws
    -> String
{
  let const working_directory = Path::current_directory().text();
  let const user = os::get_current_user().value_or(String{"???"});
  return expand_prompt_escapes(prompt, user.view(), working_directory.view(),
                               context);
}

fn build_prompt(EvalContext &context) -> String
{
  let const full_pwd = Path::current_directory().text().clone();
  set_title("shit @ " + full_pwd);

  /* The user is stable for the session, so it is resolved once and reused. The
     fallback path rescans /etc/passwd, which a per-prompt call would repeat on
     every draw in a bare-environment container. */
  static String CACHED_USER{};
  static bool was_user_resolved = false;
  if (!was_user_resolved) {
    CACHED_USER = os::get_current_user().value_or("???");
    was_user_resolved = true;
  }

  String ps1_template;
  if (Maybe<String> ps1 = context.get_variable_value("PS1");
      ps1.has_value() && !ps1->is_empty())
    ps1_template = steal(*ps1);
  else
    ps1_template = default_prompt_template();

  /* The raw template takes parameter expansion, command substitution, and
     arithmetic first, so ${debian_chroot:+...} and $(...) render fresh on
     every prompt the way bash expands PS1, and the default template's
     ${SHIT_GIT_BRANCH:+...} follows a checkout or a cd. This runs before
     the backslash escapes are decoded, so the cwd, the user, and the other
     escape-inserted text below are literal and never re-expanded. A
     directory named $(...) therefore cannot run a command at the prompt.
     The exit status is restored, since a command substitution here must not
     clobber $? for the next command. */

  let scanned_inputs = shit::ArrayList<prompt_cache_input>{};
  const bool is_cacheable =
      scan_prompt_template_inputs(ps1_template.view(), scanned_inputs);
  if (is_cacheable && PROMPT_CACHE_VALID &&
      ps1_template.view() == PROMPT_CACHE_TEMPLATE.view())
  {
    bool is_every_input_unchanged = true;
    for (let const &input : PROMPT_CACHE_INPUTS) {
      let current = context.get_variable_value(input.name.view());
      const bool both_unset = !current.has_value() && !input.value.has_value();
      const bool both_equal = current.has_value() && input.value.has_value() &&
                              current->view() == input.value->view();
      if (!both_unset && !both_equal) {
        is_every_input_unchanged = false;
        break;
      }
    }
    if (is_every_input_unchanged) {
      String rendered =
          expand_prompt_escapes(PROMPT_CACHE_EXPANSION.view(),
                                CACHED_USER.view(), full_pwd.view(), context);
      if (!colors::stdout_wants_color())
        return strip_ansi_color(rendered.view());
      return rendered;
    }
  }

  const i32 saved_status = context.last_exit_status();
  String guarded = guard_prompt_backslashes(ps1_template.view());
  String expanded;
  try {
    expanded = unguard_prompt_backslashes(
        context.expand_heredoc_body(guarded.view()).view());
  } catch (const shit::ErrorBase &) {
    /* A located error such as a command-not-found in a $(...) or an unset
       variable under set -u derives from ErrorBase rather than Error, so the
       broad base is caught. The template stands rather than letting the prompt
       draw take down the whole interactive shell. */
    expanded = ps1_template;
  }
  context.set_last_exit_status(saved_status);

  PROMPT_CACHE_VALID = false;
  if (is_cacheable) {
    for (let &input : scanned_inputs)
      input.value = context.get_variable_value(input.name.view());
    PROMPT_CACHE_TEMPLATE = ps1_template;
    PROMPT_CACHE_INPUTS = steal(scanned_inputs);
    PROMPT_CACHE_EXPANSION = expanded;
    PROMPT_CACHE_VALID = true;
  }

  String rendered = expand_prompt_escapes(expanded.view(), CACHED_USER.view(),
                                          full_pwd.view(), context);
  if (!colors::stdout_wants_color()) return strip_ansi_color(rendered.view());
  return rendered;
}

fn render_ps0(EvalContext &context) -> String
{
  Maybe<String> ps0 = context.get_variable_value("PS0");
  if (!ps0.has_value() || ps0->is_empty()) return String{};

  const i32 saved_status = context.last_exit_status();
  String guarded = guard_prompt_backslashes(ps0->view());
  String expanded;
  try {
    expanded = unguard_prompt_backslashes(
        context.expand_heredoc_body(guarded.view()).view());
  } catch (const shit::ErrorBase &) {
    context.set_last_exit_status(saved_status);
    return String{};
  }
  context.set_last_exit_status(saved_status);

  let const working_directory = Path::current_directory().text();
  let const user = os::get_current_user().value_or(String{"???"});
  String rendered = expand_prompt_escapes(expanded.view(), user.view(),
                                          working_directory.view(), context);
  if (!colors::stdout_wants_color()) return strip_ansi_color(rendered.view());
  return rendered;
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

fn set_ghost_enabled(bool enabled) -> void { unused(enabled); }

fn set_highlight_enabled(bool enabled) -> void { unused(enabled); }

fn set_edit_mode(bool is_vi) -> void { unused(is_vi); }

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

fn default_prompt_template() -> String
{
  let template_string = String{};
  const bool should_use_color = shit::colors::stdout_wants_color();

  if (should_use_color) {
    template_string += "[\\u@\\h${SHIT_GIT_BRANCH:+ (";
    template_string += shit::colors::ansi::CYAN;
    template_string += "$SHIT_GIT_BRANCH";
    template_string += shit::colors::ansi::RESET;
    template_string += ")} ";
    template_string += shit::colors::ansi::GREEN;
    template_string += "\\P";
    template_string += shit::colors::ansi::RESET;
  } else {
    template_string += "[\\u@\\h${SHIT_GIT_BRANCH:+ ($SHIT_GIT_BRANCH)} \\P";
  }
  template_string += "] ";
  return template_string;
}

fn build_prompt(shit::EvalContext &context) -> String
{
  unused(context);
  throw shit::Error{"This build has no line editor"};
}

/* A profiling build renders no prompt, so the template passes through
   unexpanded rather than walking the escape grammar. */
fn expand_prompt_template(StringView prompt, shit::EvalContext &context)
    -> String
{
  unused(context);
  return String{prompt};
}

fn render_ps0(shit::EvalContext &context) -> String
{
  unused(context);
  return String{};
}

} /* namespace toiletline */

#endif /* SHIT_NO_TOILETLINE */
