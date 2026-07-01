#include "Arena.hpp"
#include "Builtin.hpp"
#include "Colors.hpp"
#include "Completion.hpp"
#include "CompletionInternal.hpp"
#include "Debug.hpp"
#include "HashSet.hpp"
#include "Lexer.hpp"
#include "Path.hpp"
#include "Platform.hpp"
#include "Shitbox.hpp"
#include "Tokens.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

namespace shit {

namespace completion {

/* The highlighter rebuilds its spans and known-variable set every keystroke on
   this arena, reset at the top of highlight_line so the previous render stays
   valid until the editor drains it. */
static BumpArena HIGHLIGHT_ARENA{};

/* The PATH search verdicts first_word_resolves caches, keyed by the word and
   dropped when PATH changes. */
static String CACHED_PATH_VERDICT_PATH{heap_allocator()};
static StringMap<bool> PATH_SEARCH_VERDICTS{heap_allocator()};

static fn first_word_resolves(StringView word, EvalContext &context) throws
    -> bool
{
  if (word == "!") return true;
  if (KEYWORDS.find(word).has_value()) return true;

  /* A path word resolves against the filesystem, not the command-name sets, a
     leading tilde expanded first. */
  if (word.find_character('/').has_value()) {
    let expanded = String{word};
    if (!word.is_empty() && word[0] == '~') {
      if (Maybe<String> home_expanded = utils::expand_leading_tilde_path(word))
        expanded = steal(*home_expanded);
      else
        return false;
    }
    /* An existing regular file resolves even when not executable, the name is
       found rather than missing and permission is a runtime matter. */
    if (Maybe<Path> canonical = Path::canonicalize(expanded.view());
        canonical.has_value())
    {
      return canonical->is_regular_file() || canonical->is_directory();
    }
    return false;
  }

  if (search_builtin(word).has_value()) return true;
  if (context.find_function(word) != nullptr) return true;
  if (context.get_alias(word).has_value()) return true;
  /* A bare coreutil with no PATH binary still runs through the shitbox
     fallback when the toggle is on, so it resolves here the way the runtime
     resolver gates it rather than reading as a dead command. */
  if (context.shitbox() && shitbox::find_util(word).has_value()) return true;

  /* Only the PATH search verdict caches, paying the directory stats once per
     distinct word, dropped when PATH changes. A function or alias is seen live
     above. */
  if (environment_path_changed(CACHED_PATH_VERDICT_PATH))
    PATH_SEARCH_VERDICTS.clear();
  if (const bool *verdict = PATH_SEARCH_VERDICTS.find(word); verdict != nullptr)
    return *verdict;
  let const resolves = utils::search_program_path(word).count() > 0;
  LOG(All, "the path search resolves '%.*s' to %s",
      static_cast<int>(word.length), word.data, resolves ? "yes" : "no");
  PATH_SEARCH_VERDICTS.set(word, resolves);
  return resolves;
}

/* Whether the word is a strict prefix of some command name, a builtin, a
   function, an alias, or a PATH program, so an unfinished command colors yellow
   while it could still complete rather than red. */
static fn command_word_prefixes_any(StringView word,
                                    EvalContext &context) throws -> bool
{
  if (word.is_empty()) return false;
  if (word.find_character('/').has_value()) return false;

  let const has_prefix = [&](StringView name) -> bool {
    return name.starts_with(word);
  };

  for (let const &builtin_name : builtin_names())
    if (has_prefix(builtin_name.view())) return true;

  bool was_found = false;
  context.function_names().for_each([&](StringView name) {
    if (!was_found && has_prefix(name)) was_found = true;
  });
  if (was_found) return true;
  context.alias_names().for_each([&](StringView name) {
    if (!was_found && has_prefix(name)) was_found = true;
  });
  if (was_found) return true;

  for (let const &command_name : utils::path_command_names())
    if (has_prefix(command_name.view())) return true;

  return false;
}

fn command_word_resolves(StringView line, EvalContext &context) throws -> bool
{
  let const word = command_word_of(line);
  if (word.is_empty()) return true;
  return first_word_resolves(word, context);
}

static pure fn is_highlight_name_start(char c) wontthrow -> bool
{
  return lexer::is_variable_name_start(c);
}

static pure fn is_highlight_name_char(char c) wontthrow -> bool
{
  return lexer::is_variable_name(c);
}

/* The form a for loop variable must take. */
static pure fn is_plain_identifier(StringView word) wontthrow -> bool
{
  if (word.length == 0 || !is_highlight_name_start(word[0])) return false;
  for (usize i = 1; i < word.length; i++)
    if (!is_highlight_name_char(word[i])) return false;
  return true;
}

static pure fn word_defines_function(StringView line, usize word_end,
                                     usize end) wontthrow -> bool
{
  let i = word_end;
  while (i < end && (line[i] == ' ' || line[i] == '\t'))
    i++;
  if (i >= end || line[i] != '(') return false;

  i++;
  while (i < end && (line[i] == ' ' || line[i] == '\t'))
    i++;
  return i < end && line[i] == ')';
}

/* A byte that ends a top-level word, whitespace or an operator the scanner
   stops on. '{' and '}' are left out so a brace word such as a{1,2} stays one
   word. */
static pure fn is_highlight_word_break(char c) wontthrow -> bool
{
  return lexer::is_whitespace(c) || c == '\n' || c == '|' || c == '&' ||
         c == ';' || c == '<' || c == '>' || c == '(' || c == ')';
}

/* The $(...) form is handled by the caller. */
static pure fn scan_dollar_expansion(StringView line, usize dollar,
                                     usize end) wontthrow -> usize
{
  let i = dollar + 1;
  if (i >= end) return i;

  let const c = line[i];
  if (c == '{') {
    usize depth = 0;
    for (; i < end; i++) {
      if (line[i] == '{')
        depth++;
      else if (line[i] == '}') {
        i++;
        depth--;
        if (depth == 0) break;
      }
    }
    return i;
  }
  if (c >= '0' && c <= '9') {
    while (i < end && line[i] >= '0' && line[i] <= '9')
      i++;
    return i;
  }
  if (lexer::is_special_parameter_char(c)) return i + 1;
  if (is_highlight_name_start(c)) {
    while (i < end && is_highlight_name_char(line[i]))
      i++;
    return i;
  }
  return i;
}

/* A word of the form NAME=... that the parser reads as an assignment prefix
   rather than a command, so it leaves the next word in command position. */
static pure fn word_looks_like_assignment(StringView word) wontthrow -> bool
{
  if (word.length == 0 || !is_highlight_name_start(word[0])) return false;
  usize i = 1;
  while (i < word.length && is_highlight_name_char(word[i]))
    i++;
  if (i < word.length && word[i] == '=') return true;
  /* The array-element form NAME[subscript]= is also an assignment, the way the
     lexer keeps arr[i]=v one word. */
  if (i < word.length && word[i] == '[') {
    usize depth = 1;
    i++;
    while (i < word.length && depth > 0) {
      if (word[i] == '[')
        depth++;
      else if (word[i] == ']')
        depth--;
      i++;
    }
    if (i < word.length && word[i] == '=') return true;
    if (i + 1 < word.length && word[i] == '+' && word[i + 1] == '=')
      return true;
  }
  return false;
}

enum class highlight_construct : u8
{
  If,
  WhileUntil,
  For,
  Case,
  Function,
  Conditional,
};

namespace {

enum class keyword_role : u8
{
  open,
  check,
  close,
  plain,
  misplaced_in,
};

struct keyword_spec
{
  keyword_role role;
  highlight_construct construct = highlight_construct::If;
  highlight_construct construct_alt = highlight_construct::If;
  bool has_alt = false;
  bool next_is_command = false;
  bool opens_in = false;
  bool opens_for_variable = false;
  bool sets_function_pending = false;
  bool requires_non_posix = false;
};

constexpr StaticStringMap<keyword_spec>::entry HIGHLIGHT_KEYWORD_ENTRIES[] = {
    {SSK("if"),
     {.role = keyword_role::open,
      .construct = highlight_construct::If,
      .next_is_command = true}                                            },
    {SSK("while"),
     {.role = keyword_role::open,
      .construct = highlight_construct::WhileUntil,
      .next_is_command = true}                                            },
    {SSK("until"),
     {.role = keyword_role::open,
      .construct = highlight_construct::WhileUntil,
      .next_is_command = true}                                            },
    {SSK("for"),
     {.role = keyword_role::open,
      .construct = highlight_construct::For,
      .opens_in = true,
      .opens_for_variable = true}                                         },
    {SSK("case"),
     {.role = keyword_role::open,
      .construct = highlight_construct::Case,
      .opens_in = true}                                                   },
    {SSK("[["),
     {.role = keyword_role::open,
      .construct = highlight_construct::Conditional,
      .requires_non_posix = true}                                         },
    {SSK("function"),
     {.role = keyword_role::open,
      .construct = highlight_construct::Function,
      .sets_function_pending = true}                                      },
    {SSK("then"),
     {.role = keyword_role::check, .construct = highlight_construct::If}  },
    {SSK("else"),
     {.role = keyword_role::check, .construct = highlight_construct::If}  },
    {SSK("elif"),
     {.role = keyword_role::check, .construct = highlight_construct::If}  },
    {SSK("do"),
     {.role = keyword_role::check,
      .construct = highlight_construct::WhileUntil,
      .construct_alt = highlight_construct::For,
      .has_alt = true}                                                    },
    {SSK("fi"),
     {.role = keyword_role::close, .construct = highlight_construct::If}  },
    {SSK("done"),
     {.role = keyword_role::close,
      .construct = highlight_construct::WhileUntil,
      .construct_alt = highlight_construct::For,
      .has_alt = true}                                                    },
    {SSK("esac"),
     {.role = keyword_role::close, .construct = highlight_construct::Case}},
    {SSK("time"),     {.role = keyword_role::plain}                       },
    {SSK("when"),     {.role = keyword_role::plain}                       },
    {SSK("in"),       {.role = keyword_role::misplaced_in}                },
};

constexpr StaticStringMap<keyword_spec> HIGHLIGHT_KEYWORDS{
    HIGHLIGHT_KEYWORD_ENTRIES, countof(HIGHLIGHT_KEYWORD_ENTRIES)};

} // namespace

static fn scan_highlight_range(StringView line, usize begin, usize end,
                               EvalContext &context,
                               ArrayList<highlight_span> &spans,
                               HashSet &known_vars) throws -> void;

static fn color_arithmetic(StringView line, usize begin, usize end,
                           EvalContext &context,
                           ArrayList<highlight_span> &spans,
                           HashSet &known_vars) throws -> void;

static fn word_names_existing_path(StringView word) throws -> bool
{
  if (word.is_empty()) return false;
  /* A dash word is an option, never a path, sparing a stat per option each
     keystroke. */
  if (word[0] == '-') return false;
  if (word[0] == '~') {
    if (Maybe<String> expanded = utils::expand_leading_tilde_path(word);
        expanded.has_value())
      return Path{expanded->view()}.exists();
    return false;
  }
  return Path{word}.exists();
}

/* A path being typed toward a real file colors yellow rather than red. */
static fn path_partial_prefixes_entry(StringView word, usize existing_end,
                                      StringView partial, bool has_tilde) throws
    -> bool
{
  if (partial.is_empty()) return false;

  /* The directory to scan is the resolved prefix, or the root for an absolute
     path and the cwd for a relative one when nothing resolved yet. */
  String directory{heap_allocator()};
  if (existing_end > 0) {
    let const prefix = word.substring_of_length(0, existing_end);
    if (has_tilde) {
      if (Maybe<String> expanded = utils::expand_leading_tilde_path(prefix))
        directory = steal(*expanded);
      else
        return false;
    } else {
      directory = String{prefix};
    }
  } else if (word[0] == '/') {
    directory = String{"/"};
  } else {
    directory = String{"."};
  }

  let const entries = read_directory_cached(Path{directory.view()});
  if (entries == nullptr) return false;

  for (let const &entry : *entries)
    if (entry.name.view().starts_with(partial)) {
      return true;
    }

  return false;
}

/* True when the byte after a word finishes it, so no keystroke can grow it and
   an unresolved word is a dead end rather than a prefix still typed. */
static fn word_is_terminated_by_separator(StringView line, usize word_end,
                                          usize line_length) wontthrow -> bool
{
  if (word_end >= line_length) return false;

  let const next_byte = line[word_end];
  return next_byte == ' ' || next_byte == '\t' || next_byte == '\n' ||
         next_byte == ';' || next_byte == '|' || next_byte == '&';
}

/* The resolved on-disk prefix is cyan, the first unresolved segment cyan while
   it prefixes a real entry and is still being typed, red once finished or
   unmatched. Returns whether the word was treated as a path. */
static fn color_path_argument(usize word_start, StringView word,
                              bool word_is_terminated,
                              ArrayList<highlight_span> &spans) throws -> bool
{
  if (word.is_empty() || word[0] == '-') return false;

  let const has_slash = word.find_character('/').has_value();
  let const has_tilde = word[0] == '~';
  let const has_dot_prefix =
      word.length >= 2 && word[0] == '.' &&
      (word[1] == '/' ||
       (word.length >= 3 && word[1] == '.' && word[2] == '/'));

  /* A bare word with no path shape is only treated as a path when it resolves
     on disk, so an ordinary argument keeps its default color. */
  let const has_no_path_shape = !has_slash && !has_tilde && !has_dot_prefix;
  if (has_no_path_shape && !word_names_existing_path(word)) {
    return false;
  }

  /* The longest leading run of segments whose joined prefix exists on disk
     bounds the cyan span. The trailing slash rides with the prefix, so src/
     colors through the separator. The prefix is monotonic on the filesystem, a
     deeper path cannot exist when a shallower one does not, so the boundaries
     are walked from the longest down and the first that exists is the answer,
     which is one stat for a path that fully resolves rather than one per
     segment. A bare word has no internal boundary and already resolved above,
     so its whole length is the prefix without a second stat. */
  usize existing_end = 0;
  if (has_no_path_shape) {
    existing_end = word.length;
  } else {
    for (usize scan = word.length; scan >= 1; scan--) {
      let const at_boundary = scan == word.length || word[scan] == '/';
      if (!at_boundary) continue;

      let const typed_prefix = word.substring_of_length(0, scan);
      let exists = false;
      if (has_tilde) {
        if (Maybe<String> expanded =
                utils::expand_leading_tilde_path(typed_prefix);
            expanded.has_value())
          exists = Path{expanded->view()}.exists();
      } else {
        exists = Path{typed_prefix}.exists();
      }

      if (exists) {
        existing_end =
            scan < word.length && word[scan] == '/' ? scan + 1 : scan;
        break;
      }
    }
  }

  if (existing_end > 0)
    spans.push(highlight_span{word_start, word_start + existing_end,
                              colors::ansi::BRIGHT_CYAN});

  if (existing_end >= word.length) return true;

  usize segment_end = existing_end;
  while (segment_end < word.length && word[segment_end] != '/')
    segment_end++;

  let const partial =
      word.substring_of_length(existing_end, segment_end - existing_end);
  let const tail_could_complete =
      !word_is_terminated &&
      path_partial_prefixes_entry(word, existing_end, partial, has_tilde);
  let const tail_color =
      tail_could_complete ? colors::ansi::CYAN : colors::ansi::RED;
  spans.push(highlight_span{word_start + existing_end, word_start + segment_end,
                            tail_color});
  if (segment_end < word.length)
    spans.push(highlight_span{word_start + segment_end,
                              word_start + word.length, colors::ansi::RED});

  return true;
}

/* The plain variable name a simple $name or ${name} references, None when the
   expansion carries an operator such as ${x:-y} or a form like ${#x}. */
static fn simple_dollar_name(StringView line, usize i,
                             usize expansion_end) wontthrow -> Maybe<StringView>
{
  if (i + 1 >= expansion_end) return shit::None;
  if (line[i + 1] == '{') {
    if (expansion_end < i + 3 || line[expansion_end - 1] != '}')
      return shit::None;
    let inner = line.substring_of_length(i + 2, expansion_end - (i + 2) - 1);
    if (inner.is_empty()) return shit::None;
    for (usize k = 0; k < inner.length; k++)
      if (!is_highlight_name_char(inner[k])) return shit::None;
    return inner;
  }
  return line.substring_of_length(i + 1, expansion_end - (i + 1));
}

/* Whether a $ reference names something set, read without side effect so the
   highlighter never advances RANDOM or reads the clock. */
static fn dollar_name_is_set(StringView name, const HashSet &known_vars) throws
    -> bool
{
  if (name.is_empty()) return true;
  if (name.length == 1 && !is_highlight_name_start(name[0])) return true;

  if (name.is_all_decimal_digits()) return true;

  if (known_vars.contains(name)) return true;
  return os::get_environment_variable(name).has_value();
}

/* A $(...) recurses so its inner command line colors like any other. A named
   variable that is not set colors bold red. */
static fn color_dollar(StringView line, usize i, usize end,
                       ArrayList<highlight_span> &spans, EvalContext &context,
                       HashSet &known_vars) throws -> usize
{
  /* $(( ... )) frames an arithmetic expression, so its inside colors as bare
     names, numbers, and operators rather than as a command line. The two
     opening and two closing parens frame the bytes the arithmetic colorer
     reads. */
  if (i + 2 < end && line[i + 1] == '(' && line[i + 2] == '(') {
    usize depth = 0;
    let close = end;
    let j = i + 1;
    for (; j < end; j++) {
      if (line[j] == '(')
        depth++;
      else if (line[j] == ')') {
        depth--;
        if (depth == 0) {
          close = j;
          j++;
          break;
        }
      }
    }
    let const inner_begin = i + 3 < end ? i + 3 : end;
    /* A terminated $(( )) stops before its two closing parens, an unterminated
       one colors through to the end. */
    let inner_end = close < end && close >= 1 ? close - 1 : end;
    if (inner_end < inner_begin) inner_end = inner_begin;
    color_arithmetic(line, inner_begin, inner_end, context, spans, known_vars);
    return j;
  }

  if (i + 1 < end && line[i + 1] == '(') {
    usize depth = 0;
    let close = end;
    let j = i + 1;
    for (; j < end; j++) {
      if (line[j] == '(')
        depth++;
      else if (line[j] == ')') {
        depth--;
        if (depth == 0) {
          close = j;
          j++;
          break;
        }
      }
    }
    let const inner_begin = i + 2 < end ? i + 2 : end;
    let const inner_end = close < inner_begin ? inner_begin : close;
    scan_highlight_range(line, inner_begin, inner_end, context, spans,
                         known_vars);
    return j;
  }
  let const expansion_end = scan_dollar_expansion(line, i, end);
  if (expansion_end > i) {
    let sgr = colors::ansi::CYAN;
    if (Maybe<StringView> name = simple_dollar_name(line, i, expansion_end);
        name.has_value() && !dollar_name_is_set(*name, known_vars))
      sgr = colors::ansi::BOLD_RED;
    spans.push(highlight_span{i, expansion_end, sgr});
  }
  return expansion_end;
}

static fn color_arithmetic(StringView line, usize begin, usize end,
                           EvalContext &context,
                           ArrayList<highlight_span> &spans,
                           HashSet &known_vars) throws -> void
{
  usize i = begin;
  while (i < end) {
    let const c = line[i];

    if (c == '$') {
      let const next = color_dollar(line, i, end, spans, context, known_vars);
      i = next > i ? next : i + 1;
      continue;
    }

    /* A bare name is a variable read in arithmetic, no leading dollar. */
    if (is_highlight_name_start(c)) {
      let const name_start = i;
      while (i < end && is_highlight_name_char(line[i]))
        i++;
      let const name = line.substring_of_length(name_start, i - name_start);
      spans.push(highlight_span{name_start, i,
                                dollar_name_is_set(name, known_vars)
                                    ? colors::ansi::CYAN
                                    : colors::ansi::BOLD_RED});
      continue;
    }

    if (c >= '0' && c <= '9') {
      while (i < end && (is_highlight_name_char(line[i]) || line[i] == '.'))
        i++;
      continue;
    }

    if (lexer::is_whitespace(c) || c == '\n') {
      i++;
      continue;
    }

    let const operator_start = i;
    while (i < end && line[i] != '$' && !is_highlight_name_start(line[i]) &&
           !(line[i] >= '0' && line[i] <= '9') &&
           !lexer::is_whitespace(line[i]))
      i++;
    spans.push(highlight_span{operator_start, i, colors::ansi::BOLD});
  }
}

struct heredoc_pending_highlight
{
  StringView delimiter;
  bool should_strip_tabs;
};

/* The queued heredoc bodies are colored as a here-string, so the body lines are
   not mis-colored as commands. A <<- delimiter is matched once its leading tabs
   are skipped, the way the lexer strips them. */
static fn
scan_heredoc_bodies(StringView line, usize position, usize end,
                    const ArrayList<heredoc_pending_highlight> &pending,
                    ArrayList<highlight_span> &spans) throws -> usize
{
  let i = position;

  for (let const &heredoc : pending) {
    let const body_start = i;
    let was_closed = false;

    while (i < end) {
      let line_end = i;
      while (line_end < end && line[line_end] != '\n')
        line_end++;

      let content_start = i;
      if (heredoc.should_strip_tabs) {
        while (content_start < line_end && line[content_start] == '\t')
          content_start++;
      }

      let const content =
          line.substring_of_length(content_start, line_end - content_start);
      let const next = (line_end < end) ? line_end + 1 : line_end;

      if (content == heredoc.delimiter) {
        if (body_start < line_end)
          spans.push(
              highlight_span{body_start, line_end, colors::ansi::YELLOW});
        i = next;
        was_closed = true;
        break;
      }

      i = next;
    }

    if (!was_closed) {
      if (body_start < end)
        spans.push(highlight_span{body_start, end, colors::ansi::YELLOW});
      i = end;
      break;
    }
  }

  return i;
}

/* Color one command line, the window [begin, end). A command substitution
   recurses through color_dollar with its own command-position and construct
   state, so a nested command line colors on its own. */
static fn scan_highlight_range(StringView line, usize begin, usize end,
                               EvalContext &context,
                               ArrayList<highlight_span> &spans,
                               HashSet &known_vars) throws -> void
{
  let do_push = [&](usize start, usize stop, StringView sgr) throws -> void {
    if (start < stop) spans.push(highlight_span{start, stop, sgr});
  };

  let pending_assignment_names =
      ArrayList<StringView>{bump_allocator(HIGHLIGHT_ARENA)};
  let commit_pending_assignments = [&]() throws -> void {
    for (let const &name : pending_assignment_names)
      known_vars.add(name);
    pending_assignment_names.clear();
  };

  let stack = ArrayList<highlight_construct>{bump_allocator(HIGHLIGHT_ARENA)};
  let pending_heredocs =
      ArrayList<heredoc_pending_highlight>{bump_allocator(HIGHLIGHT_ARENA)};
  let is_command_position = true;
  let expecting_in = false;
  let for_variable_pending = false;
  let for_do_expected = false;
  let function_name_pending = false;
  let line_functions = HashSet{bump_allocator(HIGHLIGHT_ARENA)};

  let i = begin;
  while (i < end) {
    let const c = line[i];

    if (c == ' ' || c == '\t' || c == '\n') {
      /* A newline ends a command the way a ';' does, so the next word returns
         to command position and a keyword after it is recognized. */
      if (c == '\n') {
        commit_pending_assignments();
        is_command_position = true;
        expecting_in = false;
        i++;
        if (!pending_heredocs.is_empty()) {
          i = scan_heredoc_bodies(line, i, end, pending_heredocs, spans);
          pending_heredocs.clear();
        }
        continue;
      }
      i++;
      continue;
    }

    if (c == '#') {
      do_push(i, end, colors::ansi::DIM);
      break;
    }

    /* A << queues a heredoc body that the closing delimiter ends, while <<< is
       a one-line here-string and falls through to the operator scan. */
    if (c == '<' && i + 1 < end && line[i + 1] == '<' &&
        !(i + 2 < end && line[i + 2] == '<'))
    {
      let const operator_start = i;
      i += 2;
      let should_strip_tabs = false;
      if (i < end && line[i] == '-') {
        should_strip_tabs = true;
        i++;
      }
      do_push(operator_start, i, colors::ansi::BOLD);

      while (i < end && (line[i] == ' ' || line[i] == '\t'))
        i++;

      let const delimiter_start = i;
      while (i < end && !is_highlight_word_break(line[i]))
        i++;

      let const delimiter_word =
          line.substring_of_length(delimiter_start, i - delimiter_start);
      if (!delimiter_word.is_empty()) {
        do_push(delimiter_start, i, colors::ansi::YELLOW);

        let delimiter = delimiter_word;
        if (delimiter.length >= 2) {
          let const quote = delimiter[0];
          if ((quote == '\'' || quote == '"') &&
              delimiter[delimiter.length - 1] == quote)
          {
            delimiter = delimiter.substring_of_length(1, delimiter.length - 2);
          }
        }
        pending_heredocs.push(
            heredoc_pending_highlight{delimiter, should_strip_tabs});
      }
      continue;
    }

    /* A separator or an opener moves the next word back to command position, a
       redirection does not. */
    if (c == '|' || c == '&' || c == ';' || c == '<' || c == '>' || c == '(' ||
        c == ')' || c == '{' || c == '}')
    {
      let const operator_start = i;
      let has_separator = false;
      let has_redirect = false;
      let has_opener = false;
      let has_closer = false;
      while (i < end) {
        let const o = line[i];
        if (o == '|' || o == '&' || o == ';') {
          has_separator = true;
          i++;
        } else if (o == '<' || o == '>') {
          has_redirect = true;
          i++;
        } else if (o == '(' || o == '{') {
          has_opener = true;
          i++;
          break;
        } else if (o == ')' || o == '}') {
          has_closer = true;
          i++;
          break;
        } else {
          break;
        }
      }
      do_push(operator_start, i, colors::ansi::BOLD);

      if (has_separator || has_opener || has_closer)
        commit_pending_assignments();

      if (has_opener || (has_separator && !has_redirect)) {
        is_command_position = true;
        expecting_in = false;
      }
      continue;
    }

    let const word_start = i;
    let word_spans = ArrayList<highlight_span>{bump_allocator(HIGHLIGHT_ARENA)};
    while (i < end && !is_highlight_word_break(line[i])) {
      let const d = line[i];
      if (d == '\'') {
        let const string_start = i;
        i++;
        while (i < end && line[i] != '\'')
          i++;
        if (i < end) i++;
        word_spans.push(highlight_span{string_start, i, colors::ansi::YELLOW});
      } else if (d == '"') {
        /* literal_start tracks the start of the current yellow run, which
           begins at the opening quote and resumes after every expansion. */
        i++;
        let literal_start = i - 1;
        while (i < end && line[i] != '"') {
          if (line[i] == '\\' && i + 1 < end) {
            i += 2;
            continue;
          }
          if (line[i] == '$') {
            if (i > literal_start)
              word_spans.push(
                  highlight_span{literal_start, i, colors::ansi::YELLOW});
            i = color_dollar(line, i, end, word_spans, context, known_vars);
            literal_start = i;
            continue;
          }
          i++;
        }
        if (i < end) i++;
        if (i > literal_start)
          word_spans.push(
              highlight_span{literal_start, i, colors::ansi::YELLOW});
      } else if (d == '`') {
        /* A backtick substitution recurses like $(...). The close is the first
           unescaped backtick, a backslash escaping only a backtick, dollar, or
           backslash. */
        let const inner_begin = i + 1;
        i++;
        while (i < end && line[i] != '`') {
          if (line[i] == '\\' && i + 1 < end &&
              (line[i + 1] == '`' || line[i + 1] == '$' || line[i + 1] == '\\'))
          {
            i += 2;
          } else {
            i++;
          }
        }
        let const inner_end = i;
        if (i < end) i++;
        scan_highlight_range(line, inner_begin, inner_end, context, word_spans,
                             known_vars);
      } else if (d == '$') {
        i = color_dollar(line, i, end, word_spans, context, known_vars);
      } else if (d == '\\' && i + 1 < end) {
        i += 2;
      } else {
        i++;
      }
    }
    let const word_end = i;
    let const word =
        line.substring_of_length(word_start, word_end - word_start);
    let const plain = word_spans.is_empty();
    let const is_assignment = word_looks_like_assignment(word);

    if (is_assignment && is_command_position) {
      let assigned_name =
          word.substring_of_length(0, word.find_character('=').value());
      if (Maybe<usize> bracket = assigned_name.find_character('[');
          bracket.has_value())
        assigned_name = assigned_name.substring_of_length(0, bracket.value());
      if (is_plain_identifier(assigned_name))
        pending_assignment_names.push(assigned_name);
    }

    /* The ]] that closes a [[ conditional reads as a keyword and pops the
       construct, the way fi closes an if and done closes a loop. */
    if (plain && word == "]]" && !stack.is_empty() &&
        stack.back() == highlight_construct::Conditional)
    {
      do_push(word_start, word_end, colors::ansi::GREEN);
      stack.pop_back();
      is_command_position = false;
      continue;
    }

    if (expecting_in && plain && word == "in") {
      do_push(word_start, word_end, colors::ansi::GREEN);
      expecting_in = false;
      for_variable_pending = false;
      is_command_position = false;
      /* A for loop requires do once its word list ends, a case takes patterns,
         so this only arms for a for. */
      if (!stack.is_empty() && stack.back() == highlight_construct::For)
        for_do_expected = true;
      continue;
    }

    /* The word right after for is the loop variable, which must be a plain
       identifier, the way the parser rejects for $f. */
    if (for_variable_pending) {
      for_variable_pending = false;
      is_command_position = false;
      /* A valid loop variable is cyan, a malformed one bold red. */
      if (!plain || !is_plain_identifier(word)) {
        do_push(word_start, word_end, colors::ansi::BOLD_RED);
      } else {
        do_push(word_start, word_end, colors::ansi::CYAN);
        known_vars.add(word);
      }
      continue;
    }

    if (function_name_pending) {
      function_name_pending = false;
      is_command_position = false;
      if (plain && is_plain_identifier(word)) {
        do_push(word_start, word_end, colors::ansi::BRIGHT_BLUE);
        line_functions.add(word);
      } else {
        do_push(word_start, word_end, colors::ansi::BOLD_RED);
      }
      continue;
    }

    /* A for loop requires do once its word list ends, so a word other than do
       in that position is misplaced and shown red. */
    if (for_do_expected && is_command_position) {
      for_do_expected = false;
      if (word != "do") {
        do_push(word_start, word_end, colors::ansi::BOLD_RED);
        is_command_position = false;
        continue;
      }
    }

    if (is_command_position && plain && !is_assignment) {
      pending_assignment_names.clear();

      let is_keyword = true;
      let keyword_ok = true;
      let next_is_command = true;
      let opens_in = false;
      let opens_for_variable = false;
      if (Maybe<keyword_spec> spec = HIGHLIGHT_KEYWORDS.find(word);
          spec.has_value() &&
          !(spec.value().requires_non_posix && context.is_posix_mode()))
      {
        let const &keyword = spec.value();
        switch (keyword.role) {
        case keyword_role::open:
          stack.push(keyword.construct);
          next_is_command = keyword.next_is_command;
          opens_in = keyword.opens_in;
          opens_for_variable = keyword.opens_for_variable;
          function_name_pending = keyword.sets_function_pending;
          break;

        case keyword_role::check:
          keyword_ok =
              !stack.is_empty() &&
              (stack.back() == keyword.construct ||
               (keyword.has_alt && stack.back() == keyword.construct_alt));
          break;

        case keyword_role::close:
          keyword_ok =
              !stack.is_empty() &&
              (stack.back() == keyword.construct ||
               (keyword.has_alt && stack.back() == keyword.construct_alt));
          if (keyword_ok) stack.pop_back();
          break;

        case keyword_role::plain: break;

        case keyword_role::misplaced_in:
          keyword_ok = false;
          next_is_command = false;
          break;
        }
      } else {
        is_keyword = false;
      }

      if (is_keyword) {
        do_push(word_start, word_end,
                keyword_ok ? colors::ansi::GREEN : colors::ansi::BOLD_RED);
        is_command_position = next_is_command;
        if (opens_in) expecting_in = true;
        if (opens_for_variable) for_variable_pending = true;
        continue;
      }

      /* A command name. A path-shaped command colors per segment like a path
         argument, the existing prefix bright cyan and the part being typed cyan
         or red. A plain command name is blue once it resolves, bright blue
         while it still prefixes some command name so it could complete, and red
         once it prefixes nothing. */
      let const is_word_terminated =
          word_is_terminated_by_separator(line, word_end, end);
      if (word.find_character('/').has_value()) {
        color_path_argument(word_start, word, is_word_terminated, spans);
      } else if (word_defines_function(line, word_end, end)) {
        do_push(word_start, word_end, colors::ansi::BRIGHT_BLUE);
        line_functions.add(word);
      } else {
        let command_color = colors::ansi::RED;
        if (first_word_resolves(word, context) || line_functions.contains(word))
        {
          command_color = colors::ansi::BLUE;
        } else if (!is_word_terminated &&
                   command_word_prefixes_any(word, context))
        {
          command_color = colors::ansi::BRIGHT_BLUE;
        }
        do_push(word_start, word_end, command_color);
      }
      is_command_position = false;
      continue;
    }

    /* An assignment prefix keeps the next word in command position, an
       expansion-built command moves past it. */
    if (!is_command_position && plain && !is_assignment) {
      if (!word.is_empty() && word[0] == '-') {
        do_push(word_start, word_end, colors::ansi::GRAY);
      } else if (token_has_glob_metacharacter(word)) {
        /* An unquoted glob argument reads yellow, the word is plain here so the
           metacharacter is live rather than a quoted literal. */
        do_push(word_start, word_end, colors::ansi::YELLOW);
      } else {
        let const is_word_terminated =
            word_is_terminated_by_separator(line, word_end, end);
        color_path_argument(word_start, word, is_word_terminated, spans);
      }
    }
    for (let const &inner : word_spans)
      do_push(inner.start, inner.end, inner.sgr);
    if (is_command_position && !is_assignment) {
      pending_assignment_names.clear();
      is_command_position = false;
    }
  }

  commit_pending_assignments();
}

fn highlight_line(StringView line, EvalContext &context) throws
    -> ArrayList<highlight_span>
{
  /* Reclaim the previous keystroke's highlight allocations. The spans it
     returned have been drained into the line editor's own buffer by now. */
  HIGHLIGHT_ARENA.reset();
  let const arena = bump_allocator(HIGHLIGHT_ARENA);
  let spans = ArrayList<highlight_span>{arena};
  /* The set of named variables is read once per line so the per-expansion check
     does no allocation and triggers no dynamic-variable side effect. A line
     with no $ never references a variable, so the whole walk over the variable
     store is skipped on the common plain-command keystroke. */
  let known_vars = HashSet{arena};
  if (line.find_character('$').has_value()) {
    known_vars = context.variable_names(arena);
    let dynamic_names = ArrayList<StringView>{arena};
    context.append_dynamic_variable_names(dynamic_names);
    for (let const &name : dynamic_names)
      known_vars.add(name);
  }
  scan_highlight_range(line, 0, line.length, context, spans, known_vars);
  return spans;
}

} // namespace completion

} // namespace shit
