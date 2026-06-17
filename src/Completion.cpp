#include "Completion.hpp"
#include "CompletionInternal.hpp"

#include "Arena.hpp"
#include "Builtin.hpp"
#include "Colors.hpp"
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

/* A most-recently-used cache of directory listings, so the highlighter and TAB
   do not re-readdir the same directory on every keystroke. Keyed by the path the
   caller passed, with the mtime as the invalidation key. */
struct directory_listing_cache_entry
{
  String directory_path{};
  i64 modification_time{0};
  bool is_valid{false};
  ArrayList<cached_directory_entry> entries{};
};

static constexpr usize DIRECTORY_LISTING_CACHE_SLOT_COUNT = 4;
static directory_listing_cache_entry
    DIRECTORY_LISTING_CACHE[DIRECTORY_LISTING_CACHE_SLOT_COUNT]{};

/* The cached listing of a directory. A hit whose recorded mtime still matches
   returns the stored entries, otherwise the directory is read fresh into the
   most-recently-used slot. Returns nullptr when it cannot be read or stat'd. The
   returned pointer stays valid until the next call. */
fn read_directory_cached(const Path &directory) throws
    -> const ArrayList<cached_directory_entry> *
{
  let const path_view = directory.text().view();

  /* A directory whose mtime cannot be read is not cached, the invalidation key
     is missing. */
  os::file_status status{};
  let const has_status = os::stat_path(path_view, status);

  for (usize slot = 0; slot < DIRECTORY_LISTING_CACHE_SLOT_COUNT; slot++) {
    directory_listing_cache_entry &entry = DIRECTORY_LISTING_CACHE[slot];
    if (!entry.is_valid) continue;
    if (entry.directory_path.view() != path_view) continue;

    if (has_status && entry.modification_time == status.modification_time) {
      LOG(All, "directory listing cache hit for '%.*s'",
          static_cast<int>(path_view.length), path_view.data);
      /* Move the hit to the front, at most a three-entry shift. */
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

    /* The directory changed on disk, the stale slot is dropped. */
    entry.is_valid = false;
    break;
  }

  let listing = Path::read_directory_typed(directory);
  if (!listing.has_value()) return nullptr;

  /* The directory flag is resolved once per read from the dirent type. Only a
     symlink or an unknown type falls back to a stat. */
  let resolved_entries = ArrayList<cached_directory_entry>{};
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

  /* The ring shifts down by one and the fresh listing lands at the front. */
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

  /* A directory with no readable mtime is read but left invalid, a later
     keystroke re-reads rather than trust an unkeyed slot. */
  return &DIRECTORY_LISTING_CACHE[0].entries;
}

/* True for a byte that separates one shell word from the next at the top level.
   The completion tokenizer is deliberately coarse, it does not parse quotes or
   operators, since a prefix completion only needs the run of bytes the cursor
   sits in. */
static pure fn is_word_separator(char c) wontthrow -> bool
{
  return lexer::is_whitespace(c) || c == '\n';
}

/* True for a byte that ends one command and begins another, so the word right
   after it is again in command position. */
static pure fn is_command_separator(char c) wontthrow -> bool
{
  return c == ';' || c == '|' || c == '&' || c == '(' || c == '\n';
}

/* Whether the closing paren at position matches no opener earlier on the line,
   the shape of a case pattern's closing paren. A matched paren closes a subshell
   or a substitution instead. */
static pure fn is_unmatched_closing_paren(StringView line,
                                          usize position) wontthrow -> bool
{
  usize depth = 0;
  for (usize k = 0; k < position; k++) {
    if (line[k] == '(')
      depth++;
    else if (line[k] == ')' && depth > 0)
      depth--;
  }
  return depth == 0;
}

/* True when an unquoted glob metacharacter appears in the token, so TAB
   resolves the glob rather than listing a directory. */
static pure fn token_has_glob_metacharacter(StringView token) wontthrow -> bool
{
  for (usize i = 0; i < token.length; i++) {
    let const c = token[i];
    if (c == '*' || c == '?' || c == '[') return true;
  }
  return false;
}

/* True for a byte that ends the token under the cursor, a word separator or a
   command separator, so `ls|gre` leaves `gre` under the cursor. */
static pure fn is_token_boundary(char c) wontthrow -> bool
{
  return is_word_separator(c) || is_command_separator(c);
}

/* The byte offset where the token under the cursor begins. The scan walks back
   from the cursor over non-boundary bytes. */
static pure fn find_token_start(StringView line, usize cursor) wontthrow
    -> usize
{
  let start = cursor;
  while (start > 0 && !is_token_boundary(line[start - 1]))
    start--;
  return start;
}

/* The byte offset just past the token the cursor sits inside, so a replacement
   covers the whole word rather than the bytes left of the cursor. */
static pure fn find_token_end(StringView line, usize cursor) wontthrow -> usize
{
  let end = cursor;
  while (end < line.length && !is_token_boundary(line[end]))
    end++;
  return end;
}

/* Whether the token starting at token_start sits in command position, the first
   word of a command. The scan looks back over whitespace to the previous
   non-space byte and reports command position when that byte ends a command or
   the token is the very first on the line. */
/* The leading words a command word can follow, the ! and time keywords, the
   compound keywords whose body opens with a command, and the wrapper commands
   whose argument is itself a command the way fish skips sudo. for, case, and in
   stay opaque since a subject word or patterns follow them. */
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
    TRANSPARENT_PREFIX_ENTRIES,
    sizeof(TRANSPARENT_PREFIX_ENTRIES) / sizeof(TRANSPARENT_PREFIX_ENTRIES[0])};

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
  for (;;) {
    while (i > 0 && is_word_separator(line[i - 1]))
      i--;
    if (i == 0) return true;
    if (is_command_separator(line[i - 1])) return true;
    /* A case pattern's closing paren opens the arm's body. */
    if (line[i - 1] == ')' && is_unmatched_closing_paren(line, i - 1))
      return true;
    /* A transparent keyword prefix is stepped over, the word after time or ! is
       still a command word. */
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

/* A glob-active mask with every position set, for an unquoted completion token
   whose every byte is a live glob metacharacter. */
static fn all_active_glob_mask(usize length) throws -> ArrayList<bool>
{
  let mask = ArrayList<bool>{};
  mask.reserve(length);
  for (usize i = 0; i < length; i++)
    mask.push(true);
  return mask;
}

/* A command-position token matches a command name as a plain prefix, or as a
   glob pattern when the token holds a metacharacter, so ec* lists the commands
   it matches. */
static fn command_name_matches(StringView name, StringView token,
                               bool token_is_glob) throws -> bool
{
  if (!token_is_glob) return name.starts_with(token);

  let const glob_active = all_active_glob_mask(token.length);
  return utils::glob_matches(token, name, glob_active, 0);
}

/* Append name to candidates when it matches the token and the seen set has not
   recorded it, keeping the merged list deduped across builtins, functions,
   aliases, and PATH at one hash lookup per insert. */
static fn add_unique_command(ArrayList<String> &candidates, HashSet &seen,
                             StringView name, StringView token,
                             bool token_is_glob) throws -> void
{
  if (!command_name_matches(name, token, token_is_glob)) return;
  if (seen.contains(name)) return;
  seen.add(name);
  candidates.push(String{name});
}

/* The longest prefix shared by every candidate. With one candidate it is that
   candidate, with none it is empty. */
static fn
compute_longest_common_prefix(const ArrayList<String> &candidates) throws
    -> String
{
  if (candidates.is_empty()) return String{};
  let prefix = String{candidates[0].view()};
  for (usize i = 1; i < candidates.count(); i++) {
    const String &candidate = candidates[i];
    usize shared = 0;
    while (shared < prefix.length() && shared < candidate.length() &&
           prefix.view()[shared] == candidate.view()[shared])
    {
      shared++;
    }
    /* The byte-wise match can stop inside a multibyte codepoint, so the cut
       retracts to the codepoint start to avoid corrupting a glyph in the ghost
       text. */
    while (shared > 0 && shared < prefix.length() &&
           (static_cast<unsigned char>(prefix.view()[shared]) & 0xC0) == 0x80)
    {
      shared--;
    }
    prefix = String{prefix.view().substring_of_length(0, shared)};
  }
  return prefix;
}

/* Collect every command name that matches the typed token. The builtins come
   from the registry name list, the functions and aliases from the context, and
   the executables from a scan of the PATH directories. A token holding a glob
   metacharacter matches by glob, so a bare glob first word lists the command
   names it matches rather than cwd entries. */
/* The PATH executable names, cached so the per-keystroke ghost does not read
   every PATH directory on each keystroke. The cache rebuilds only when the PATH
   value changes, which an interactive session does rarely, so a freeze on a
   large PATH becomes a one-time cost rather than a per-key one. */
static String CACHED_COMPLETION_PATH{};
static ArrayList<String> CACHED_PATH_COMMANDS{};
static bool is_cached_path_commands_valid = false;

/* Whether the live PATH differs from the cached copy, updating the copy on a
   change. */
fn environment_path_changed(String &cached_path) throws -> bool
{
  const char *path = std::getenv("PATH");
  let const current = path != nullptr ? StringView{path} : StringView{};
  if (cached_path.view() == current) return false;
  cached_path = String{current};
  return true;
}

fn path_command_names() throws -> const ArrayList<String> &
{
  if (!environment_path_changed(CACHED_COMPLETION_PATH) &&
      is_cached_path_commands_valid)
  {
    return CACHED_PATH_COMMANDS;
  }

  /* A directory repeated in PATH is read only once, a layered profile does not
     multiply the scan. */
  let const current = CACHED_COMPLETION_PATH.view();
  CACHED_PATH_COMMANDS.clear();
  let seen_directories = HashSet{heap_allocator()};
  usize segment_start = 0;
  for (usize i = 0; i <= current.length; i++) {
    if (i != current.length && current[i] != os::PATH_DELIMITER) continue;
    let const segment =
        current.substring_of_length(segment_start, i - segment_start);
    segment_start = i + 1;
    let const dir_view = segment.is_empty() ? StringView{"."} : segment;
    if (seen_directories.contains(dir_view)) continue;
    seen_directories.add(dir_view);
    let const directory = Path{dir_view};
    if (Maybe<ArrayList<String>> entries = Path::read_directory(directory)) {
      for (let &entry : *entries)
        CACHED_PATH_COMMANDS.push(steal(entry));
    }
  }
  is_cached_path_commands_valid = true;
  LOG(Info, "rebuilt the path command cache, %zu names",
      CACHED_PATH_COMMANDS.count());
  return CACHED_PATH_COMMANDS;
}

static fn complete_command(StringView token, bool token_is_glob,
                           EvalContext &context) throws -> ArrayList<String>
{
  let candidates = ArrayList<String>{};
  let seen = HashSet{heap_allocator()};

  TRACELN("completing command position for token '%.*s'",
          static_cast<int>(token.length), token.data);

  for (let const &builtin_name : builtin_names()) {
    add_unique_command(candidates, seen, builtin_name.view(), token,
                       token_is_glob);
  }

  /* The bundled shitbox utility names resolve as commands when the shitbox
     option is on. */
  if (context.shitbox()) {
    for (const String &util_name : shitbox::util_names())
      add_unique_command(candidates, seen, util_name.view(), token,
                         token_is_glob);
  }

  context.function_names().for_each([&](StringView name) {
    add_unique_command(candidates, seen, name, token, token_is_glob);
  });

  context.alias_names().for_each([&](StringView name) {
    add_unique_command(candidates, seen, name, token, token_is_glob);
  });

  for (let const &entry : path_command_names())
    add_unique_command(candidates, seen, entry.view(), token, token_is_glob);

  LOG(All, "collected %zu command candidates for token '%.*s'",
      candidates.count(), static_cast<int>(token.length), token.data);

  return candidates;
}

/* Split a filesystem token into the directory part and the partial basename.
   The directory part keeps its trailing separator so the basename joins back
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

/* Resolve the directory the token's directory part names into a real disk path,
   expanding a leading tilde and rooting a relative path at base_directory. */
static fn resolve_listing_directory(StringView directory_part,
                                    const Path &base_directory) throws -> Path
{
  if (directory_part.is_empty()) return base_directory;

  /* A leading tilde expands to a home directory, the current user's for ~ or
     the named user's for ~user. An unknown name leaves the tilde literal. */
  if (directory_part[0] == '~') {
    usize name_end = 1;
    while (name_end < directory_part.length && directory_part[name_end] != '/')
      name_end++;
    let const name = directory_part.substring_of_length(1, name_end - 1);
    let home = name.is_empty() ? os::get_home_directory()
                               : os::get_home_for_user(name);
    if (home.has_value()) {
      let resolved_path = home->clone();
      /* Drop the name and the separator after it, then append the rest. */
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

/* Complete a filesystem token. The directory part is listed and every entry
   whose name carries the partial basename becomes a candidate, with the
   directory part prefixed back on and a trailing slash on a directory. */
static fn complete_filesystem(StringView token,
                              const Path &base_directory) throws
    -> ArrayList<String>
{
  let candidates = ArrayList<String>{};

  path_token parts = split_path_token(token);

  TRACELN(
      "completing filesystem token '%.*s', dir '%.*s', base '%.*s'",
      static_cast<int>(token.length), token.data,
      static_cast<int>(parts.basename_part.length), parts.basename_part.data,
      static_cast<int>(parts.basename_part.length), parts.basename_part.data);

  let listing_directory =
      resolve_listing_directory(parts.directory_part, base_directory);

  /* The listing reuses the directory cache the highlighter just filled. */
  let const entries = read_directory_cached(listing_directory);
  if (entries == nullptr) return candidates;

  for (let const &entry : *entries) {
    let const name = entry.name.view();
    if (!name.starts_with(parts.basename_part)) continue;

    /* A dotfile stays hidden unless the user typed a leading dot. */
    if (name.length > 0 && name[0] == '.' &&
        (parts.basename_part.is_empty() || parts.basename_part[0] != '.'))
    {
      continue;
    }

    let candidate = String{parts.directory_part};
    candidate += name;

    /* The cached directory flag spares a stat for the trailing slash. */
    if (entry.is_directory) candidate += '/';

    candidates.push(steal(candidate));
  }

  LOG(All, "%zu entries of '%s' match basename '%.*s'", candidates.count(),
      listing_directory.text().c_str(),
      static_cast<int>(parts.basename_part.length), parts.basename_part.data);

  return candidates;
}

/* Resolve a glob token into its matches by listing the directory the fixed
   prefix names and keeping every entry the pattern matches. Only the trailing
   component is globbed. */
static fn complete_glob(StringView token, const Path &base_directory) throws
    -> ArrayList<String>
{
  let candidates = ArrayList<String>{};

  path_token parts = split_path_token(token);

  TRACELN("resolving glob token '%.*s'", static_cast<int>(token.length),
          token.data);

  let listing_directory =
      resolve_listing_directory(parts.directory_part, base_directory);

  let entries = Path::read_directory(listing_directory);
  if (!entries.has_value()) return candidates;

  /* Every byte of the basename pattern is an active glob position, since the
     completion token is unquoted. */
  let const glob_active = all_active_glob_mask(parts.basename_part.length);

  for (let const &entry : *entries) {
    /* A dotfile only matches a pattern the user began with a dot. */
    if (entry.length() > 0 && entry.view()[0] == '.' &&
        (parts.basename_part.is_empty() || parts.basename_part[0] != '.'))
    {
      continue;
    }

    if (!utils::glob_matches(parts.basename_part, entry.view(), glob_active, 0))
    {
      continue;
    }

    let candidate = String{parts.directory_part};
    candidate += entry.view();

    let full = listing_directory.clone();
    full.push_component(entry.view());
    if (full.is_directory()) candidate += '/';

    candidates.push(steal(candidate));
  }

  LOG(All, "glob pattern '%.*s' matched %zu entries",
      static_cast<int>(token.length), token.data, candidates.count());

  return candidates;
}

/* Whether the token names a variable expansion, a leading '$'. */
static pure fn token_is_variable(StringView token) wontthrow -> bool
{
  return token.length >= 1 && token[0] == '$';
}

/* Complete a variable token. The name after the '$' (or '${') becomes a prefix
   matched against the shell variable names and the environment names, and each
   match is formatted back into the form the user typed, '$NAME' or '${NAME}'.
   The brace form is only offered when the typed token has no name characters
   after a metacharacter the completion does not parse, so a plain '$' or '${'
   prefix completes cleanly. */
static fn complete_variable(StringView token, EvalContext &context) throws
    -> ArrayList<String>
{
  let candidates = ArrayList<String>{};

  /* Strip the leading '$' and an optional '{'. The brace form is reproduced on
     every candidate. */
  let has_brace = token.length >= 2 && token[1] == '{';
  usize name_start = has_brace ? 2 : 1;
  let const prefix = token.substring(name_start);

  TRACELN("completing variable token '%.*s', prefix '%.*s', brace %d",
          static_cast<int>(token.length), token.data,
          static_cast<int>(prefix.length), prefix.data, has_brace ? 1 : 0);

  let seen = HashSet{heap_allocator()};

  let do_add_name = [&](StringView name) throws -> void {
    if (!name.starts_with(prefix)) return;
    if (seen.contains(name)) return;
    seen.add(name);

    let candidate = String{};
    candidate += has_brace ? "${" : "$";
    candidate.append(name);
    if (has_brace) candidate.push('}');
    candidates.push(steal(candidate));
  };

  context.variable_names().for_each(
      [&](StringView name) { do_add_name(name); });

  for (let const &name : os::environment_names())
    do_add_name(name.view());

  LOG(All, "%zu variable names match prefix '%.*s'", candidates.count(),
      static_cast<int>(prefix.length), prefix.data);

  return candidates;
}

/* A leading ~ with no / yet completes against the system user names. Once a /
   appears the filesystem handler takes over. */
static fn token_is_tilde_user_prefix(StringView token) wontthrow -> bool
{
  return token.length >= 1 && token[0] == '~' &&
         !token.find_character('/').has_value();
}

/* Complete a ~name token against the system users, each candidate written back
   as ~name with a trailing / since a home is a directory. */
static fn complete_tilde_user(StringView token) throws -> ArrayList<String>
{
  let candidates = ArrayList<String>{};
  let const prefix = token.substring(1);
  for (let const &user : os::enumerate_users()) {
    if (!user.view().starts_with(prefix)) continue;
    let candidate = String{};
    candidate.push('~');
    candidate.append(user.view());
    candidate.push('/');
    candidates.push(steal(candidate));
  }
  LOG(All, "%zu user names match tilde prefix '%.*s'", candidates.count(),
      static_cast<int>(prefix.length), prefix.data);
  return candidates;
}

/* The command word of the line's last segment, past any transparent keyword
   prefix, so echo hi; git - completes against git. The separator scan is
   unquoted. */
fn command_word_of(StringView line) wontthrow -> StringView
{
  usize i = 0;
  usize open_parens = 0;
  for (usize k = 0; k < line.length; k++) {
    let const c = line[k];
    if (c == '(') {
      open_parens++;
    } else if (c == ')') {
      /* A matched paren closes a substitution or a subshell mid-word, while
         an unmatched one closes a case pattern and starts the arm's body. */
      if (open_parens > 0)
        open_parens--;
      else
        i = k + 1;
    } else if (c == ';' || c == '|' || c == '&') {
      i = k + 1;
    }
  }
  for (;;) {
    while (i < line.length && (line[i] == ' ' || line[i] == '\t'))
      i++;
    let const start = i;
    while (i < line.length && line[i] != ' ' && line[i] != '\t')
      i++;
    let const word = line.substring_of_length(start, i - start);
    if (word.is_empty() || !is_transparent_command_prefix(word)) return word;
  }
}

/* Expand a command name through alias definitions only, the first word of each
   expansion, bounded against a cyclic alias. Symlinks are left alone so a name
   that dispatches on its argv[0], such as a busybox or rustup link, keeps the
   surface name the user typed. */
fn resolve_completion_alias(StringView command,
                            EvalContext &context) throws -> String
{
  let name = String{command};
  for (int depth = 0; depth < 8; depth++) {
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

fn resolve_completion_command(StringView command,
                                     EvalContext &context) throws -> String
{
  let name = resolve_completion_alias(command, context);
  /* A name holding a slash is a path already, otherwise it is searched on
     PATH, then the located file's symlinks are followed to the real target. */
  let const located = utils::search_program_path(name.view());
  if (!located.is_empty()) {
    if (let const canonical = os::canonical_path(located.front());
        canonical.has_value())
      return String{canonical->filename()};
  }
  return name;
}

/* Split the line into whitespace words for COMP_WORDS, reporting the index of
   the cursor's word. An empty trailing word is appended when the cursor is past
   the last one. */
fn split_completion_words(StringView line, usize cursor,
                                 usize &cword) throws -> ArrayList<String>
{
  let words = ArrayList<String>{};
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
    words.push(String{line.substring_of_length(start, i - start)});
  }
  if (!is_found) {
    cword = words.count();
    words.push(String{});
  }
  return words;
}

flatten fn complete(StringView line, usize cursor, EvalContext &context,
                    const Path &base_directory, bool for_listing) throws
    -> completion_result
{
  if (cursor > line.length) cursor = line.length;

  /* When the cursor sits inside a command substitution, completion re-roots to
     the substitution's own command line, so echo $(git che and `git che and
     for x in $(git che all offer git's subcommands rather than the outer
     command's arguments. The offset maps the replaced token span back to the
     full line for the caller. */
  let const completion_offset = command_substitution_body_start(line, cursor);
  line = line.substring(completion_offset);
  cursor -= completion_offset;

  /* The replaced span covers the whole word the cursor sits inside, from the
     token start to the token end, so a cursor in the middle of a word replaces
     the word cleanly rather than keeping the bytes to its right. */
  let token_start = find_token_start(line, cursor);
  let const token_end = find_token_end(line, cursor);
  let token = line.substring_of_length(token_start, token_end - token_start);
  let const is_command = is_in_command_position(line, token_start);

  /* An option-value word such as --exit-node=host completes only the value
     after the equals sign, so the candidate replaces the value and keeps the
     flag rather than overwriting the whole --flag= prefix. The value opens
     with no dash, so the option-name stages defer and the word reaches the
     spec and the filesystem, the way bash splits on the equals through
     COMP_WORDBREAKS. A command-position word is left whole, since an
     assignment such as name=value is its own token there. */
  if (!is_command && token.length >= 2 && token[0] == '-')
    if (let const equals = token.find_character('='); equals.has_value()) {
      token_start = token_start + *equals + 1;
      token = line.substring_of_length(token_start, token_end - token_start);
    }

  let const token_is_glob = token_has_glob_metacharacter(token);

  /* A command-position token holding a path separator is a program given by
     path, so it completes against the filesystem rather than the command sets. */
  let const token_has_path_separator = token.find_character('/').has_value();

  TRACELN("complete line '%.*s' cursor %zu token '%.*s' command %d",
          static_cast<int>(line.length), line.data, cursor,
          static_cast<int>(token.length), token.data, is_command ? 1 : 0);

  /* A glob word with the cursor right after it expands inline to its file
     matches, even in command position. */
  let const inline_glob = token_is_glob && cursor == token_end;

  let candidates = ArrayList<String>{};
  /* Filled only by the --help option and subcommand stages, keyed by candidate
     text so it survives the sort below, empty for every other source. */
  let descriptions = StringMap<String>{heap_allocator()};

  /* The POSIX mood keeps completion plain like dash, command names in command
     position and the filesystem elsewhere, with no variable, tilde, manpage, or
     spec stage. */
  let const is_posix_completion = context.mood() == mimic_mood::Posix;

  if (token_is_variable(token) && !is_posix_completion) {
    candidates = complete_variable(token, context);
  } else if (token_is_tilde_user_prefix(token) && !is_posix_completion) {
    candidates = complete_tilde_user(token);
  } else if (inline_glob) {
    candidates = complete_glob(token, base_directory);
    if (!candidates.is_empty()) {
      /* The matches replace the pattern as one space-joined run of fields, the
         listing-UI trailing slash dropped. */
      utils::sort_ascending(candidates);
      let joined = String{};
      for (usize i = 0; i < candidates.count(); i++) {
        if (i > 0) joined += ' ';
        let match = candidates[i].view();
        if (!match.is_empty() && match[match.length - 1] == '/')
          match = match.substring_of_length(0, match.length - 1);
        joined.append(match);
      }
      candidates.clear();
      candidates.push(steal(joined));
    } else if (is_command && !token_has_path_separator) {
      /* A command-position glob that matches no file falls back to matching
         command names, so ec* still finds echo. */
      candidates = complete_command(token, token_is_glob, context);
    }
  } else if (is_command && !token_has_path_separator) {
    /* An empty command token would enumerate every PATH command on each
       keystroke for the ghost, freezing a large PATH, so command completion
       runs only once a prefix is typed. An explicit tab still lists them all. */
    if (!token.is_empty() || for_listing)
      candidates = complete_command(token, token_is_glob, context);
  } else if (token_is_glob) {
    candidates = complete_glob(token, base_directory);
  } else {
    /* The argument cascade runs in the bash and the default moods, the POSIX
       mood goes straight to files. The builtin flag tables answer first, then
       the registered specs, then the man sources, then the build tools, and the
       --help fork is the last resort. */
    Maybe<ArrayList<String>> from_stage = None;
    if (!is_posix_completion) {
      from_stage =
          complete_from_builtin_flags(line, token, token_start, context);
      if (!from_stage.has_value())
        from_stage = complete_from_spec(line, token, cursor, for_listing,
                                        context, descriptions);
      if (!from_stage.has_value())
        from_stage = complete_from_man_subcommands(line, token, token_start,
                                                   for_listing, context);
      if (!from_stage.has_value())
        from_stage = complete_from_manpage(line, token, for_listing, context,
                                           descriptions);
      if (!from_stage.has_value())
        from_stage = complete_from_build_tools(line, token, token_start,
                                               for_listing, context);
      if (!from_stage.has_value())
        from_stage = complete_from_help_subcommands(
            line, token, token_start, for_listing, context, descriptions);
      if (!from_stage.has_value())
        from_stage =
            complete_from_help(line, token, for_listing, context, descriptions);
    }
    if (from_stage.has_value()) {
      candidates = steal(*from_stage);
    } else if (for_listing || !split_path_token(token).basename_part.is_empty())
    {
      /* A token ending in a slash has an empty basename, so the ghost would
         list a whole directory to suggest nothing. The listing runs for the
         ghost only once a basename is typed, while an explicit tab still lists. */
      candidates = complete_filesystem(token, base_directory);
    }
  }

  /* A token that matched nothing skips the sort and the prefix scan. */
  let longest_common_prefix = String{};
  if (!candidates.is_empty()) {
    utils::sort_ascending(candidates);
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

} /* namespace completion */

} /* namespace shit */
