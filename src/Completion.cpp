#include "Completion.hpp"

#include "Arena.hpp"
#include "Builtin.hpp"
#include "Colors.hpp"
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

struct directory_listing_cache_entry
{
  String directory_path{heap_allocator()};
  i64 modification_time{0};
  bool is_valid{false};
  ArrayList<cached_directory_entry> entries{heap_allocator()};
};

static constexpr usize DIRECTORY_LISTING_CACHE_SLOT_COUNT = 4;
static directory_listing_cache_entry
    DIRECTORY_LISTING_CACHE[DIRECTORY_LISTING_CACHE_SLOT_COUNT]{};

BumpArena COMPLETION_ARENA{};

/* The returned pointer stays valid until the next call. */
fn read_directory_cached(const Path &directory) throws
    -> const ArrayList<cached_directory_entry> *
{
  let const path_view = directory.text().view();

  os::file_status status{};
  let const has_status = os::stat_path(path_view, status);

  for (usize slot = 0; slot < DIRECTORY_LISTING_CACHE_SLOT_COUNT; slot++) {
    directory_listing_cache_entry &entry = DIRECTORY_LISTING_CACHE[slot];
    if (!entry.is_valid) continue;
    if (entry.directory_path.view() != path_view) continue;

    if (has_status && entry.modification_time == status.modification_time) {
      LOG(All, "directory listing cache hit for '%.*s'",
          static_cast<int>(path_view.length), path_view.data);
      if (slot != 0) {
        directory_listing_cache_entry hit = steal(entry);
        for (usize back = slot; back > 0; back--) {
          DIRECTORY_LISTING_CACHE[back] =
              steal(DIRECTORY_LISTING_CACHE[back - 1]);
        }
        DIRECTORY_LISTING_CACHE[0] = steal(hit);
        return &DIRECTORY_LISTING_CACHE[0].entries;
      }
      return &entry.entries;
    }

    entry.is_valid = false;
    break;
  }

  let listing = Path::read_directory_typed(directory);
  if (!listing.has_value()) return nullptr;

  let resolved_entries = ArrayList<cached_directory_entry>{heap_allocator()};
  resolved_entries.reserve(listing->count());
  for (let &child : *listing) {
    let is_directory = false;
    switch (child.kind) {
    case Path::entry_kind::Directory: is_directory = true; break;
    case Path::entry_kind::Regular:
    case Path::entry_kind::Other: is_directory = false; break;
    default: {
      let full = directory.clone();
      full.push_component(child.name.view());
      is_directory = full.is_directory();
      break;
    }
    }

    resolved_entries.push(
        cached_directory_entry{steal(child.name), is_directory});
  }

  for (usize back = DIRECTORY_LISTING_CACHE_SLOT_COUNT - 1; back > 0; back--)
    DIRECTORY_LISTING_CACHE[back] = steal(DIRECTORY_LISTING_CACHE[back - 1]);

  directory_listing_cache_entry fresh{};
  fresh.directory_path = String{path_view};
  fresh.modification_time = has_status ? status.modification_time : 0;
  fresh.is_valid = has_status;
  fresh.entries = steal(resolved_entries);
  DIRECTORY_LISTING_CACHE[0] = steal(fresh);

  LOG(All, "directory listing cache miss for '%.*s', read %zu entries",
      static_cast<int>(path_view.length), path_view.data,
      DIRECTORY_LISTING_CACHE[0].entries.count());

  /* A directory with no readable mtime is left invalid so a later keystroke
     re-reads rather than trust an unkeyed slot. */
  return &DIRECTORY_LISTING_CACHE[0].entries;
}

static pure fn is_word_separator(char c) wontthrow -> bool
{
  return lexer::is_whitespace(c) || c == '\n';
}

static pure fn is_command_separator(char c) wontthrow -> bool
{
  return c == ';' || c == '|' || c == '&' || c == '(' || c == '\n';
}

static pure fn is_unmatched_closing_paren(StringView line,
                                          usize position) wontthrow -> bool
{
  usize depth = 0;
  for (usize k = 0; k < position; k++) {
    if (line[k] == '(') {
      depth++;
    } else if (line[k] == ')' && depth > 0) {
      depth--;
    }
  }
  return depth == 0;
}

pure fn token_has_glob_metacharacter(StringView token) wontthrow -> bool
{
  for (usize i = 0; i < token.length; i++) {
    let const c = token[i];
    if (c == '*' || c == '?' || c == '[') return true;
  }
  return false;
}

static pure fn is_token_boundary(char c) wontthrow -> bool
{
  return is_word_separator(c) || is_command_separator(c);
}

static pure fn find_token_start(StringView line, usize cursor) wontthrow
    -> usize
{
  let start = cursor;
  while (start > 0 && !is_token_boundary(line[start - 1]))
    start--;
  return start;
}

static pure fn find_token_end(StringView line, usize cursor) wontthrow -> usize
{
  let end = cursor;
  while (end < line.length && !is_token_boundary(line[end]))
    end++;
  return end;
}

struct open_quote_span
{
  usize content_start;
  char quote_character;
};

static pure fn find_open_quote(StringView line, usize cursor) wontthrow
    -> Maybe<open_quote_span>
{
  char quote_character = 0;
  usize content_start = 0;

  for (usize i = 0; i < cursor; i++) {
    let const c = line[i];
    if (quote_character == 0) {
      if (c == '\'' || c == '"') {
        quote_character = c;
        content_start = i + 1;
      }
    } else if (c == quote_character) {
      quote_character = 0;
    }
  }

  if (quote_character == 0) return None;

  return open_quote_span{content_start, quote_character};
}

/* for, case, and in stay opaque since a subject word or patterns follow them.
 */
static constexpr StaticStringMap<bool>::entry TRANSPARENT_PREFIX_ENTRIES[] = {
    {SSK("!"),       true},
    {SSK("time"),    true},
    {SSK("if"),      true},
    {SSK("then"),    true},
    {SSK("else"),    true},
    {SSK("elif"),    true},
    {SSK("while"),   true},
    {SSK("until"),   true},
    {SSK("do"),      true},
    {SSK("{"),       true},
    {SSK("sudo"),    true},
    {SSK("doas"),    true},
    {SSK("env"),     true},
    {SSK("nice"),    true},
    {SSK("command"), true},
    {SSK("builtin"), true},
};
static constexpr StaticStringMap<bool> TRANSPARENT_PREFIXES{
    TRANSPARENT_PREFIX_ENTRIES, countof(TRANSPARENT_PREFIX_ENTRIES)};

static pure fn is_transparent_command_prefix(StringView word) wontthrow -> bool
{
  if (word.is_empty()) return false;
  if (word[0] == '-') return true;
  if (lexer::is_variable_name_start(word[0]) &&
      word.find_character('=').has_value())
  {
    return true;
  }
  return TRANSPARENT_PREFIXES.find(word).has_value();
}

static pure fn is_in_command_position(StringView line,
                                      usize token_start) wontthrow -> bool
{
  let i = token_start;
  loop
  {
    while (i > 0 && is_word_separator(line[i - 1]))
      i--;
    if (i == 0) return true;
    if (is_command_separator(line[i - 1])) return true;
    if (line[i - 1] == ')' && is_unmatched_closing_paren(line, i - 1)) {
      return true;
    }

    let word_start = i;
    while (word_start > 0 && !is_word_separator(line[word_start - 1]) &&
           !is_command_separator(line[word_start - 1]))
      word_start--;
    if (!is_transparent_command_prefix(
            line.substring_of_length(word_start, i - word_start)))
      return false;
    i = word_start;
  }
}

static fn all_active_glob_mask(usize length) throws -> Bitset
{
  let mask = Bitset{completion_allocator()};
  mask.reserve(length);
  for (usize i = 0; i < length; i++)
    mask.push(true);
  return mask;
}

static fn command_name_matches(StringView name, StringView token,
                               bool token_is_glob,
                               const Bitset &glob_active) throws -> bool
{
  if (!token_is_glob) return name.starts_with(token);

  return utils::glob_matches(token, name, glob_active, 0);
}

static fn add_unique_command(ArrayList<String> &candidates, HashSet &seen,
                             StringView name, StringView token,
                             bool token_is_glob,
                             const Bitset &glob_active) throws -> void
{
  if (!command_name_matches(name, token, token_is_glob, glob_active)) return;
  if (seen.contains(name)) return;
  seen.add(name);
  candidates.push(String{completion_allocator(), name});
}

static fn
compute_longest_common_prefix(const ArrayList<String> &candidates) throws
    -> String
{
  if (candidates.is_empty()) return String{candidates.allocator()};
  let const first = candidates[0].view();
  usize prefix_length = first.length;
  for (usize i = 1; i < candidates.count(); i++) {
    let const candidate = candidates[i].view();
    usize shared = 0;
    while (shared < prefix_length && shared < candidate.length &&
           first[shared] == candidate[shared])
    {
      shared++;
    }
    /* The byte-wise match can stop inside a multibyte codepoint, so the cut
       retracts to the codepoint start. */
    while (shared > 0 && shared < prefix_length &&
           (static_cast<unsigned char>(first[shared]) & 0xC0) == 0x80)
    {
      shared--;
    }
    prefix_length = shared;
  }
  return String{candidates.allocator(),
                first.substring_of_length(0, prefix_length)};
}

fn environment_path_changed(String &cached_path) throws -> bool
{
  const char *path = std::getenv("PATH");
  let const current = path != nullptr ? StringView{path} : StringView{};
  if (cached_path.view() == current) return false;
  cached_path = String{current};
  return true;
}

static fn complete_command(StringView token, bool token_is_glob,
                           EvalContext &context) throws -> ArrayList<String>
{
  let candidates = ArrayList<String>{completion_allocator()};
  let seen = HashSet{completion_allocator()};

  TRACELN("completing command position for token '%.*s'",
          static_cast<int>(token.length), token.data);

  let const glob_active = token_is_glob ? all_active_glob_mask(token.length)
                                        : Bitset{completion_allocator()};

  for (let const &builtin_name : builtin_names()) {
    add_unique_command(candidates, seen, builtin_name.view(), token,
                       token_is_glob, glob_active);
  }

  if (context.shitbox()) {
    for (const String &util_name : shitbox::util_names())
      add_unique_command(candidates, seen, util_name.view(), token,
                         token_is_glob, glob_active);
  }

  context.function_names().for_each([&](StringView name) {
    add_unique_command(candidates, seen, name, token, token_is_glob,
                       glob_active);
  });

  context.alias_names().for_each([&](StringView name) {
    add_unique_command(candidates, seen, name, token, token_is_glob,
                       glob_active);
  });

  for (let const &entry : utils::path_command_names())
    add_unique_command(candidates, seen, entry.view(), token, token_is_glob,
                       glob_active);

  LOG(All, "collected %zu command candidates for token '%.*s'",
      candidates.count(), static_cast<int>(token.length), token.data);

  return candidates;
}

/* The directory part keeps its trailing separator so the basename joins back
   on. */
struct path_token
{
  StringView directory_part;
  StringView basename_part;
};

static pure fn split_path_token(StringView token) wontthrow -> path_token
{
  let last_separator = token.length;
  for (usize i = 0; i < token.length; i++) {
    if (token[i] == '/') last_separator = i;
  }
  if (last_separator == token.length) {
    return path_token{StringView{}, token};
  }
  return path_token{
      token.substring_of_length(0, last_separator + 1),
      token.substring(last_separator + 1),
  };
}

/* The tilde is excluded since it expands a home the user wants. */
static pure fn path_candidate_needs_quoting(StringView candidate) wontthrow
    -> bool
{
  for (usize i = 0; i < candidate.length; i++) {
    let const c = candidate[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '*' || c == '?' ||
        c == '[' || c == ']' || c == '(' || c == ')' || c == '{' || c == '}' ||
        c == '\'' || c == '"' || c == '`' || c == '$' || c == '&' || c == '|' ||
        c == ';' || c == '<' || c == '>' || c == '\\' || c == '!' || c == '#')
    {
      return true;
    }
  }
  return false;
}

/* An embedded single quote ends the run, emits an escaped quote, and reopens,
   the standard '\'' idiom. */
static fn quote_path_candidate(StringView candidate) throws -> String
{
  let quoted = String{completion_allocator()};
  quoted.push('\'');
  for (usize i = 0; i < candidate.length; i++) {
    if (candidate[i] == '\'') {
      quoted += "'\\''";
    } else {
      quoted.push(candidate[i]);
    }
  }
  quoted.push('\'');
  return quoted;
}

static fn resolve_listing_directory(StringView directory_part,
                                    const Path &base_directory) throws -> Path
{
  if (directory_part.is_empty()) return base_directory;

  /* An unknown name leaves the tilde literal. */
  if (directory_part[0] == '~') {
    usize name_end = 1;
    while (name_end < directory_part.length && directory_part[name_end] != '/')
      name_end++;
    let const name = directory_part.substring_of_length(1, name_end - 1);
    let home = name.is_empty() ? os::get_home_directory()
                               : os::get_home_for_user(name);
    if (home.has_value()) {
      let resolved_path = home->clone();
      let rest_start = name_end;
      if (rest_start < directory_part.length &&
          directory_part[rest_start] == '/')
      {
        rest_start++;
      }
      if (rest_start < directory_part.length) {
        resolved_path.push_component(directory_part.substring(rest_start));
      }
      return resolved_path;
    }
  }

  let const directory = Path{directory_part};
  if (directory.is_absolute()) return directory;

  let resolved_path = base_directory.clone();
  resolved_path.push_component(directory_part);
  return resolved_path;
}

static fn complete_filesystem(StringView token, const Path &base_directory,
                              bool inside_quote, bool directories_only) throws
    -> ArrayList<String>
{
  let candidates = ArrayList<String>{completion_allocator()};

  path_token parts = split_path_token(token);

  TRACELN(
      "completing filesystem token '%.*s', dir '%.*s', base '%.*s'",
      static_cast<int>(token.length), token.data,
      static_cast<int>(parts.directory_part.length), parts.directory_part.data,
      static_cast<int>(parts.basename_part.length), parts.basename_part.data);

  let listing_directory =
      resolve_listing_directory(parts.directory_part, base_directory);

  let const entries = read_directory_cached(listing_directory);
  if (entries == nullptr) return candidates;

  for (let const &entry : *entries) {
    let const name = entry.name.view();
    if (!name.starts_with(parts.basename_part)) continue;

    if (directories_only && !entry.is_directory) continue;

    /* A dotfile stays hidden unless the user typed a leading dot. */
    if (name.length > 0 && name[0] == '.' &&
        (parts.basename_part.is_empty() || parts.basename_part[0] != '.'))
    {
      continue;
    }

    let candidate = String{completion_allocator(), parts.directory_part};
    candidate += name;

    if (entry.is_directory) candidate += '/';

    /* A token already inside a quote is not quoted again. */
    if (!inside_quote && path_candidate_needs_quoting(candidate.view())) {
      candidate = quote_path_candidate(candidate.view());
    }

    candidates.push(steal(candidate));
  }

  LOG(All, "%zu entries of '%s' match basename '%.*s'", candidates.count(),
      listing_directory.text().c_str(),
      static_cast<int>(parts.basename_part.length), parts.basename_part.data);

  return candidates;
}

/* Only the trailing component is globbed. */
static fn complete_glob(StringView token, const Path &base_directory,
                        bool directories_only) throws -> ArrayList<String>
{
  let candidates = ArrayList<String>{completion_allocator()};

  path_token parts = split_path_token(token);

  TRACELN("resolving glob token '%.*s'", static_cast<int>(token.length),
          token.data);

  let listing_directory =
      resolve_listing_directory(parts.directory_part, base_directory);

  let const entries = read_directory_cached(listing_directory);
  if (entries == nullptr) return candidates;

  let const glob_active = all_active_glob_mask(parts.basename_part.length);

  for (let const &entry : *entries) {
    let const name = entry.name.view();
    if (!name.is_empty() && name[0] == '.' &&
        (parts.basename_part.is_empty() || parts.basename_part[0] != '.'))
    {
      continue;
    }

    if (!utils::glob_matches(parts.basename_part, name, glob_active, 0)) {
      continue;
    }

    if (directories_only && !entry.is_directory) continue;

    let candidate = String{completion_allocator(), parts.directory_part};
    candidate += name;
    if (entry.is_directory) candidate += '/';

    candidates.push(steal(candidate));
  }

  LOG(All, "glob pattern '%.*s' matched %zu entries",
      static_cast<int>(token.length), token.data, candidates.count());

  return candidates;
}

static pure fn token_is_variable(StringView token) wontthrow -> bool
{
  return !token.is_empty() && token[0] == '$';
}

static fn complete_variable(StringView token, EvalContext &context) throws
    -> ArrayList<String>
{
  let candidates = ArrayList<String>{completion_allocator()};

  let has_brace = token.length >= 2 && token[1] == '{';
  usize name_start = has_brace ? 2 : 1;
  let const prefix = token.substring(name_start);

  TRACELN("completing variable token '%.*s', prefix '%.*s', brace %d",
          static_cast<int>(token.length), token.data,
          static_cast<int>(prefix.length), prefix.data, has_brace ? 1 : 0);

  let seen = HashSet{completion_allocator()};

  let do_add_name = [&](StringView name) throws -> void {
    if (!name.starts_with(prefix)) return;
    if (seen.contains(name)) return;
    seen.add(name);

    let candidate = String{completion_allocator()};
    candidate += has_brace ? "${" : "$";
    candidate.append(name);
    if (has_brace) candidate.push('}');
    candidates.push(steal(candidate));
  };

  context.variable_names().for_each(
      [&](StringView name) { do_add_name(name); });

  for (let const &name : os::environment_names())
    do_add_name(name.view());

  let dynamic_names = ArrayList<StringView>{completion_allocator()};
  context.append_dynamic_variable_names(dynamic_names);
  for (let const &name : dynamic_names)
    do_add_name(name);

  LOG(All, "%zu variable names match prefix '%.*s'", candidates.count(),
      static_cast<int>(prefix.length), prefix.data);

  return candidates;
}

static fn token_is_tilde_user_prefix(StringView token) wontthrow -> bool
{
  return !token.is_empty() && token[0] == '~' &&
         !token.find_character('/').has_value();
}

static fn complete_tilde_user(StringView token) throws -> ArrayList<String>
{
  let candidates = ArrayList<String>{completion_allocator()};
  let const prefix = token.substring(1);
  for (let const &user : os::enumerate_users()) {
    if (!user.view().starts_with(prefix)) continue;
    let candidate = String{completion_allocator()};
    candidate.push('~');
    candidate.append(user.view());
    candidate.push('/');
    candidates.push(steal(candidate));
  }
  LOG(All, "%zu user names match tilde prefix '%.*s'", candidates.count(),
      static_cast<int>(prefix.length), prefix.data);
  return candidates;
}

fn command_word_of(StringView line) wontthrow -> StringView
{
  usize i = 0;
  usize open_parens = 0;
  for (usize k = 0; k < line.length; k++) {
    let const c = line[k];
    if (c == '(') {
      open_parens++;
    } else if (c == ')') {
      /* An unmatched paren closes a case pattern and starts the arm's body. */
      if (open_parens > 0)
        open_parens--;
      else
        i = k + 1;
    } else if (c == ';' || c == '|' || c == '&') {
      i = k + 1;
    }
  }
  loop
  {
    while (i < line.length && (line[i] == ' ' || line[i] == '\t'))
      i++;
    let const start = i;
    while (i < line.length && line[i] != ' ' && line[i] != '\t')
      i++;
    let const word = line.substring_of_length(start, i - start);
    if (word.is_empty() || !is_transparent_command_prefix(word)) return word;
  }
}

/* Symlinks are left alone so a name that dispatches on its argv[0], such as a
   busybox or rustup link, keeps the surface name the user typed. */
fn resolve_completion_alias(StringView command, EvalContext &context) throws
    -> String
{
  let name = String{command};
  for (usize depth = 0; depth < 8; depth++) {
    let const expansion = context.get_alias(name.view());
    if (!expansion.has_value()) break;
    let const expanded = expansion->view();
    usize i = 0;
    while (i < expanded.length && (expanded[i] == ' ' || expanded[i] == '\t'))
      i++;
    let const start = i;
    while (i < expanded.length && expanded[i] != ' ' && expanded[i] != '\t')
      i++;
    let const first = expanded.substring_of_length(start, i - start);
    if (first.is_empty() || first == name.view()) break;
    name = String{first};
  }
  return name;
}

fn resolve_completion_command(StringView command, EvalContext &context) throws
    -> String
{
  let name = resolve_completion_alias(command, context);
  let const located = utils::search_program_path(name.view());
  if (!located.is_empty()) {
    if (let const canonical = os::canonical_path(located.front());
        canonical.has_value())
      return String{canonical->filename()};
  }
  return name;
}

fn split_completion_words(StringView line, usize cursor, usize &cword) throws
    -> ArrayList<String>
{
  let words = ArrayList<String>{completion_allocator()};
  usize i = 0;
  let is_found = false;
  while (i < line.length) {
    while (i < line.length && (line[i] == ' ' || line[i] == '\t'))
      i++;
    if (i >= line.length) break;
    let const start = i;
    while (i < line.length && line[i] != ' ' && line[i] != '\t')
      i++;
    if (cursor >= start && cursor <= i) {
      cword = words.count();
      is_found = true;
    }
    words.push(String{completion_allocator(),
                      line.substring_of_length(start, i - start)});
  }
  if (!is_found) {
    cword = words.count();
    words.push(String{completion_allocator()});
  }
  return words;
}

flatten fn complete(StringView line, usize cursor, EvalContext &context,
                    const Path &base_directory, bool for_listing) throws
    -> completion_result
{
  COMPLETION_ARENA.reset();
  let const arena = completion_allocator();

  if (cursor > line.length) cursor = line.length;

  /* When the cursor sits inside a command substitution, completion re-roots to
     the substitution's own command line. The offset maps the replaced token
     span back to the full line for the caller. */
  let const completion_offset = command_substitution_body_start(line, cursor);
  line = line.substring(completion_offset);
  cursor -= completion_offset;

  let token_start = find_token_start(line, cursor);
  let token_end = find_token_end(line, cursor);
  let token = line.substring_of_length(token_start, token_end - token_start);
  let const is_command = is_in_command_position(line, token_start);

  /* A cursor inside an open quote completes the bare path within it and is not
     re-quoted. The span leaves any closing quote to the right untouched. */
  let const open_quote = find_open_quote(line, cursor);
  if (open_quote.has_value()) {
    token_start = open_quote->content_start;
    token_end = cursor;
    token = line.substring_of_length(token_start, token_end - token_start);
  }

  /* An option-value word such as --exit-node=host completes only the value
     after the equals sign, the way bash splits on the equals through
     COMP_WORDBREAKS. A command-position word is left whole, since an assignment
     such as name=value is its own token there. */
  if (!open_quote.has_value() && !is_command && token.length >= 2 &&
      token[0] == '-')
    if (let const equals = token.find_character('='); equals.has_value()) {
      token_start = token_start + *equals + 1;
      token = line.substring_of_length(token_start, token_end - token_start);
    }

  let const token_is_glob = token_has_glob_metacharacter(token);

  /* A command-position token holding a path separator completes against the
     filesystem rather than the command sets. */
  let const token_has_path_separator = token.find_character('/').has_value();

  TRACELN("complete line '%.*s' cursor %zu token '%.*s' command %d",
          static_cast<int>(line.length), line.data, cursor,
          static_cast<int>(token.length), token.data, is_command ? 1 : 0);

  /* A glob word with the cursor right after it expands inline to its file
     matches, even in command position. */
  let const inline_glob = token_is_glob && cursor == token_end;

  let const directories_only = !is_command && command_word_of(line) == "cd";

  let candidates = ArrayList<String>{arena};
  let descriptions = StringMap<String>{arena};

  let const is_posix_completion = context.mood() == mimic_mood::Posix;

  if (token_is_variable(token) && !is_posix_completion) {
    candidates = complete_variable(token, context);
  } else if (token_is_tilde_user_prefix(token) && !is_posix_completion) {
    candidates = complete_tilde_user(token);
  } else if (inline_glob) {
    candidates = complete_glob(token, base_directory, directories_only);
    if (!candidates.is_empty()) {
      candidates.sort();
      let joined = String{arena};
      for (usize i = 0; i < candidates.count(); i++) {
        if (i > 0) joined += ' ';
        let match = candidates[i].view();
        if (!match.is_empty() && match[match.length - 1] == '/') {
          match = match.substring_of_length(0, match.length - 1);
        }
        joined.append(match);
      }
      candidates.clear();
      candidates.push(steal(joined));
    } else if (is_command && !token_has_path_separator) {
      candidates = complete_command(token, token_is_glob, context);
    }
  } else if (is_command && !token_has_path_separator) {
    /* An empty command token would enumerate every PATH command on each
       keystroke for the ghost, so command completion runs only once a prefix
       is typed. An explicit tab still lists them all. */
    if (!token.is_empty() || for_listing)
      candidates = complete_command(token, token_is_glob, context);
  } else if (token_is_glob) {
    candidates = complete_glob(token, base_directory, directories_only);
  } else {
    /* The argument cascade runs in the bash and the default moods, the POSIX
       mood goes straight to files. The build tools answer before the man
       sources, so a recognized build tool in the current directory offers its
       targets even when a like-named subcommand man page exists. */
    Maybe<ArrayList<String>> from_stage = None;
    if (!is_posix_completion) {
      from_stage =
          complete_from_builtin_flags(line, token, token_start, context);
      if (!from_stage.has_value())
        from_stage = complete_from_spec(line, token, cursor, for_listing,
                                        context, descriptions);
      if (!from_stage.has_value())
        from_stage = complete_from_tools_with_targets(line, token, token_start,
                                                      for_listing, context);
      if (!from_stage.has_value())
        from_stage = complete_from_man_subcommands(line, token, token_start,
                                                   for_listing, context);
      if (!from_stage.has_value())
        from_stage = complete_from_manpage(line, token, for_listing, context,
                                           descriptions);
      if (!from_stage.has_value())
        from_stage = complete_from_help_subcommands(
            line, token, token_start, for_listing, context, descriptions);
      if (!from_stage.has_value())
        from_stage = complete_from_help(line, token, token_start, for_listing,
                                        context, descriptions);
    }
    if (from_stage.has_value()) {
      candidates = steal(*from_stage);
    } else if (for_listing || !split_path_token(token).basename_part.is_empty())
    {
      /* A token ending in a slash has an empty basename, so the ghost listing
         runs only once a basename is typed. An explicit tab still lists. */
      candidates = complete_filesystem(
          token, base_directory, open_quote.has_value(), directories_only);
    }
  }

  let longest_common_prefix = String{arena};
  if (!candidates.is_empty()) {
    candidates.sort();

    let unique_candidates = ArrayList<String>{candidates.allocator()};
    unique_candidates.reserve(candidates.count());

    for (usize i = 0; i < candidates.count(); i++) {
      if (unique_candidates.is_empty() ||
          unique_candidates.back().view() != candidates[i].view())
        unique_candidates.push(steal(candidates[i]));
    }
    candidates = steal(unique_candidates);

    longest_common_prefix = compute_longest_common_prefix(candidates);
  }

  return completion_result{
      steal(candidates),
      steal(descriptions),
      steal(longest_common_prefix),
      token_start + completion_offset,
      token_end + completion_offset,
      is_command,
  };
}

} // namespace completion

} // namespace shit
