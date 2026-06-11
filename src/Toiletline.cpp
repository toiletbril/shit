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

#include <cstdlib>
#include <cstring>
#include <ctime>

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

    shit::completion::completion_result result = shit::completion::complete(
        line, byte_cursor, *COMPLETION_CONTEXT, base, for_listing != 0);

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
  } catch (shit::ErrorBase &error) {
    LOG(shit::verbosity::Debug, "completion swallowed an error: %s",
        error.message().c_str());
    return 0;
  } catch (...) {
    LOG(shit::verbosity::Debug, "completion swallowed an unknown throw");
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
  if (!home) return shit::None;
  let path = home->clone();
  path.push_component(SHIT_HISTORY_FILE);
  return path;
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
  /* The highlighter is registered unconditionally and gates itself on the live
     color decision, so a runtime change to NO_COLOR takes effect without
     re-registering. The callback returns no spans when color is off. */
  ::tl_set_highlight_callback(shit_highlight_callback);
}

fn disable_completion() -> void
{
  COMPLETION_CONTEXT = nullptr;
  ::tl_set_complete_callback(nullptr);
  ::tl_set_highlight_callback(nullptr);
}

fn set_ghost_enabled(bool enabled) -> void
{
  ::tl_set_ghost_enabled(enabled ? 1 : 0);
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

/* The cwd shown in a prompt is shortened from the front with a leading ... once
   it runs past this many bytes, so a deep path does not push the cursor across
   the terminal. */
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

/* The current git branch read from .git/HEAD without forking git, walking up
   from the working directory to the filesystem root. Empty outside a
   repository. A detached HEAD shows the short commit hash. */
static fn git_branch() throws -> String
{
  let dir = Path::current_directory();
  for (;;) {
    let head = dir.clone();
    head.push_component(".git");
    /* A linked worktree or a submodule stores .git as a file holding a
       'gitdir: <path>' pointer rather than a directory, so the real git dir is
       followed before reading HEAD. */
    let git_dir = head.clone();
    if (let const dot_git = utils::read_entire_file(head.text().view())) {
      let const pointer = dot_git->view();
      let const gitdir_prefix = StringView{"gitdir: "};
      if (pointer.starts_with(gitdir_prefix)) {
        let line = pointer.substring(gitdir_prefix.length);
        while (!line.is_empty() &&
               (line[line.length - 1] == '\n' || line[line.length - 1] == '\r'))
        {
          line = line.substring_of_length(0, line.length - 1);
        }
        let resolved_gitdir = Path{line};
        /* A relative gitdir pointer is relative to the directory holding the
           .git file, not the current directory. */
        if (!resolved_gitdir.is_absolute()) {
          resolved_gitdir = dir;
          resolved_gitdir.push_component(line);
        }
        git_dir = steal(resolved_gitdir);
      }
    }
    let git_head = git_dir.clone();
    git_head.push_component("HEAD");
    if (let const content = utils::read_entire_file(git_head.text().view())) {
      let text = content->view();
      while (!text.is_empty() &&
             (text[text.length - 1] == '\n' || text[text.length - 1] == '\r'))
      {
        text = text.substring_of_length(0, text.length - 1);
      }
      let const ref_prefix = StringView{"ref: refs/heads/"};
      if (text.starts_with(ref_prefix))
        return String{text.substring(ref_prefix.length)};
      return String{
          text.substring_of_length(0, text.length < 7 ? text.length : 7)};
    }
    let parent = dir.clone();
    parent.push_component("..");
    let normalized = parent.to_absolute().normalized();
    if (normalized.text() == dir.text()) break;
    dir = steal(normalized);
  }
  return String{};
}

/* A human duration for the \D prompt segment, quiet under a few milliseconds,
   then milliseconds, then seconds with one decimal. */
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

/* The hostname, the full name for need_full and the part before the first dot
   otherwise, falling back to the HOSTNAME variable and then localhost. */
static fn prompt_hostname(bool need_full) throws -> String
{
  String host = os::get_hostname().value_or(
      os::get_environment_variable("HOSTNAME").value_or("localhost"));
  if (need_full) return host;
  usize dot = 0;
  while (dot < host.length() && host.view()[dot] != '.')
    dot++;
  return String{host.view().substring_of_length(0, dot)};
}

/* Collapse a leading home directory to ~ the way bash does, only when the home
   prefix ends on a path boundary so HOME=/home/sd and cwd=/home/sderp keeps the
   full path rather than rendering ~erp. */
static fn collapse_home_prefix(StringView path) throws -> String
{
  let shown = String{path};
  Maybe<Path> home = os::get_home_directory();
  if (home && shown.starts_with(home->text()) &&
      (shown.length() == home->count() || shown.view()[home->count()] == '/'))
  {
    let collapsed = String{};
    collapsed += "~";
    collapsed += shown.substring(home->count());
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

    /* An octal escape \nnn takes up to three octal digits after the backslash
       and emits the byte they name, the way bash does. */
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
    /* \h is the hostname up to the first dot, \H is the full name. */
    case 'h': out += prompt_hostname(false); break;
    case 'H': out += prompt_hostname(true); break;
    case 'w': out += collapse_home_prefix(working_directory); break;
    case 'W': out += Path{working_directory}.filename(); break;
    /* The working directory with the home collapsed to ~ and then shortened
       from the front with an ellipsis, the form the default prompt uses so a
       deep path stays short. */
    case 'P':
      out += shorten_path_with_ellipsis(
          collapse_home_prefix(working_directory).view(), PROMPT_PWD_LENGTH);
      break;
    /* The git branch wrapped in parentheses with a trailing space when the cwd
       is inside a work tree, empty otherwise, the form the default prompt uses.
       The bare \g escape stays the unwrapped name for a custom prompt. */
    case 'G': {
      let const branch = git_branch();
      if (!branch.is_empty()) {
        out += " (";
        out += branch.view();
        out += ")";
      }
    } break;
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
    /* \s is the shell name, the basename of the zeroth positional parameter. */
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
    /* The last exit status, colored green on success and red on failure when
       the terminal takes color. */
    case '?': {
      const i32 status = context.last_exit_status();
      const bool wants_color = colors::stdout_wants_color();
      if (wants_color)
        out += status == 0 ? colors::ansi::GREEN : colors::ansi::RED;
      out += utils::int_to_text(status);
      if (wants_color) out += colors::ansi::RESET;
    } break;
    /* The number of background jobs. */
    case 'j':
      out += utils::int_to_text(static_cast<i64>(context.jobs().count()));
      break;
    /* The time the last command took, empty for an instant command. */
    case 'D':
      out += format_prompt_duration(context.last_command_duration_ns());
      break;
    /* The current git branch, empty outside a repository. */
    case 'g': out += git_branch(); break;
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

/* The decoded prompt and the result of its parameter pass from the previous
   draw. While the decoded prompt is unchanged the expansion is reused, so a
   command substitution in PS1 does not run again until a prompt input moves. */
static String PROMPT_CACHE_KEY{};
static String PROMPT_CACHE_VALUE{};

fn default_prompt_template() -> String
{
  let template_string = String{};
  template_string += R"(\s@\u@\h )";
  if (colors::stdout_wants_color()) {
    template_string += colors::ansi::GREEN;
    template_string += R"(\P)";
    template_string += colors::ansi::RESET;
  } else {
    template_string += R"(\P)";
  }
  template_string += R"(\G $ )";
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

fn build_prompt(EvalContext &context) -> String
{
  let const full_pwd = Path::current_directory().text().clone();
  set_title("shit @ " + full_pwd);

  /* The user is stable for the session, so it is resolved once and reused. The
     fallback path rescans /etc/passwd, which a per-prompt call would repeat on
     every draw in a bare-environment container. */
  static String CACHED_USER{};
  static bool USER_RESOLVED = false;
  if (!USER_RESOLVED) {
    CACHED_USER = os::get_current_user().value_or("???");
    USER_RESOLVED = true;
  }

  /* The PS1 template, the user's when set, otherwise the built-in default. */
  String ps1_template;
  if (Maybe<String> ps1 = context.get_variable_value("PS1");
      ps1.has_value() && !ps1->is_empty())
    ps1_template = steal(*ps1);
  else
    ps1_template = default_prompt_template();

  /* The raw template takes parameter expansion, command substitution, and
     arithmetic first, so ${debian_chroot:+...} and $(...) render. This runs
     before the backslash escapes are decoded, so the cwd, the user, and the
     other escape-inserted text below are literal and never re-expanded. A
     directory named $(...) therefore cannot run a command at the prompt. The
     result is cached on the raw template and the exit status is restored, since
     a command substitution here must not clobber $? for the next command. The
     backslash escapes are decoded last on either path, inserting the cwd, the
     user, and the clock as literal text the expansion never sees. */
  if (ps1_template.view() == PROMPT_CACHE_KEY.view()) {
    String rendered =
        expand_prompt_escapes(PROMPT_CACHE_VALUE.view(), CACHED_USER.view(),
                              full_pwd.view(), context);
    if (!colors::stdout_wants_color()) return strip_ansi_color(rendered.view());
    return rendered;
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
  PROMPT_CACHE_KEY = ps1_template;
  PROMPT_CACHE_VALUE = expanded;
  String rendered = expand_prompt_escapes(expanded.view(), CACHED_USER.view(),
                                          full_pwd.view(), context);
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
  template_string += "\\s@\\u@\\h ";
  if (shit::colors::stdout_wants_color()) {
    template_string += shit::colors::ansi::GREEN;
    template_string += "\\P";
    template_string += shit::colors::ansi::RESET;
  } else {
    template_string += "\\P";
  }
  template_string += "\\G \\$ ";
  return template_string;
}

fn build_prompt(shit::EvalContext &context) -> String
{
  unused(context);
  throw shit::Error{"This build has no line editor"};
}

} /* namespace toiletline */

#endif /* SHIT_NO_TOILETLINE */
