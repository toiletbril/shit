#include "Completion.hpp"

#include "Builtin.hpp"
#include "Debug.hpp"
#include "HashSet.hpp"
#include "Platform.hpp"
#include "Utils.hpp"

namespace shit {

namespace completion {

/* True for a byte that separates one shell word from the next at the top level.
   The completion tokenizer is deliberately coarse, it does not parse quotes or
   operators, since a prefix completion only needs the run of bytes the cursor
   sits in. */
static pure fn is_word_separator(char c) wontthrow -> bool
{
  return c == ' ' || c == '\t' || c == '\n';
}

/* True for a byte that ends one command and begins another, so the word right
   after it is again in command position. */
static pure fn is_command_separator(char c) wontthrow -> bool
{
  return c == ';' || c == '|' || c == '&' || c == '(' || c == '\n';
}

/* True when an unquoted glob metacharacter appears in the token, so TAB
   resolves the glob rather than listing a directory. */
static pure fn token_has_glob_metacharacter(StringView token) wontthrow -> bool
{
  for (usize i = 0; i < token.length; i++) {
    const char c = token[i];
    if (c == '*' || c == '?' || c == '[') return true;
  }
  return false;
}

/* The byte offset where the token under the cursor begins. The scan walks back
   from the cursor over non-separator bytes. */
static pure fn find_token_start(StringView line, usize cursor) wontthrow
    -> usize
{
  usize start = cursor;
  while (start > 0 && !is_word_separator(line[start - 1]))
    start--;
  return start;
}

/* Whether the token starting at token_start sits in command position, the first
   word of a command. The scan looks back over whitespace to the previous
   non-space byte and reports command position when that byte ends a command or
   the token is the very first on the line. */
static pure fn is_in_command_position(StringView line,
                                      usize token_start) wontthrow -> bool
{
  usize i = token_start;
  while (i > 0 && is_word_separator(line[i - 1]))
    i--;
  if (i == 0) return true;
  return is_command_separator(line[i - 1]);
}

/* Append name to candidates when it carries the typed prefix and is not already
   present, so the merged command list stays deduped across builtins, functions,
   aliases, and PATH. */
static fn add_unique_with_prefix(ArrayList<String> &candidates, StringView name,
                                 StringView prefix) throws -> void
{
  if (!name.starts_with(prefix)) return;
  for (const String &existing : candidates) {
    if (existing.view() == name) return;
  }
  candidates.push(String{name});
}

/* The longest prefix shared by every candidate. With one candidate it is that
   candidate, with none it is empty. */
static fn
compute_longest_common_prefix(const ArrayList<String> &candidates) throws
    -> String
{
  if (candidates.is_empty()) return String{};
  String prefix{candidates[0].view()};
  for (usize i = 1; i < candidates.count(); i++) {
    const String &candidate = candidates[i];
    usize shared = 0;
    while (shared < prefix.length() && shared < candidate.length() &&
           prefix.view()[shared] == candidate.view()[shared])
    {
      shared++;
    }
    prefix = String{prefix.view().substring_of_length(0, shared)};
  }
  return prefix;
}

/* The builtin command names offered in command position. The packed keys in
   BUILTIN_ENTRIES cannot hand back their bytes, so the names are spelled here
   as plain literals, kept in step with that table by hand. */
static constexpr const char *BUILTIN_NAMES[] = {
    "echo",    "exit",     "cd",     "pwd",     "which",  "whoami", "export",
    "break",   "continue", "return", ":",       "true",   "false",  "test",
    "[",       ".",        "source", "eval",    "set",    "shift",  "unset",
    "read",    "printf",   "umask",  "getopts", "trap",   "exec",   "type",
    "command", "readonly", "local",  "times",   "ulimit", "hash",   "alias",
    "unalias", "jobs",     "fg",     "bg",      "wait",   "kill",
};

/* Collect every command name with the typed prefix. The builtins come from the
   static name list, the functions and aliases from the context, and the
   executables from a scan of the PATH directories. */
static fn complete_command(StringView prefix, EvalContext &context) throws
    -> ArrayList<String>
{
  ArrayList<String> candidates{};

  TRACELN("completing command position for prefix '%.*s'",
          static_cast<int>(prefix.length), prefix.data);

  for (const char *builtin_name : BUILTIN_NAMES) {
    add_unique_with_prefix(candidates, StringView{builtin_name}, prefix);
  }

  context.function_names().for_each([&](StringView name) {
    add_unique_with_prefix(candidates, name, prefix);
  });

  context.alias_names().for_each([&](StringView name) {
    add_unique_with_prefix(candidates, name, prefix);
  });

  /* A prefix that names a path component, not a bare command word, is a program
     given by path, so the PATH scan would not find it. The filesystem branch
     handles it as an argument instead, so the command scan stays a name scan.
   */
  if (prefix.find_character('/').has_value()) {
    return candidates;
  }

  if (Maybe<String> path = os::get_environment_variable("PATH");
      path.has_value())
  {
    StringView path_view = path->view();
    usize segment_start = 0;
    for (usize i = 0; i <= path_view.length; i++) {
      if (i != path_view.length && path_view[i] != os::PATH_DELIMITER) continue;

      StringView segment =
          path_view.substring_of_length(segment_start, i - segment_start);
      segment_start = i + 1;

      const Path directory{segment.is_empty() ? StringView{"."} : segment};
      Maybe<ArrayList<String>> entries = Path::read_directory(directory);
      if (!entries.has_value()) continue;

      for (const String &entry : *entries) {
        add_unique_with_prefix(candidates, entry.view(), prefix);
      }
    }
  }

  return candidates;
}

/* Split a filesystem token into the directory part and the partial basename.
   The directory part keeps its trailing separator, so joining the basename back
   on reproduces the token. A token with no separator has an empty directory
   part and the whole token as the basename. */
struct path_token
{
  StringView directory_part;
  StringView basename_part;
};

static pure fn split_path_token(StringView token) wontthrow -> path_token
{
  usize last_separator = token.length;
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

  /* A leading tilde with no user name expands to the home directory, the only
     tilde form the completion handles. */
  if (directory_part[0] == '~') {
    if (Maybe<Path> home = os::get_home_directory(); home.has_value()) {
      Path resolved = *home;
      /* Drop the tilde and the separator after it, then append the rest. */
      usize rest_start = 1;
      if (rest_start < directory_part.length &&
          directory_part[rest_start] == '/')
      {
        rest_start++;
      }
      if (rest_start < directory_part.length) {
        resolved.push_component(directory_part.substring(rest_start));
      }
      return resolved;
    }
  }

  const Path directory{directory_part};
  if (directory.is_absolute()) return directory;

  Path resolved = base_directory;
  resolved.push_component(directory_part);
  return resolved;
}

/* Complete a filesystem token. The directory part is listed and every entry
   whose name carries the partial basename becomes a candidate, with the
   directory part prefixed back on and a trailing slash on a directory. */
static fn complete_filesystem(StringView token,
                              const Path &base_directory) throws
    -> ArrayList<String>
{
  ArrayList<String> candidates{};

  path_token parts = split_path_token(token);

  TRACELN(
      "completing filesystem token '%.*s', dir '%.*s', base '%.*s'",
      static_cast<int>(token.length), token.data,
      static_cast<int>(parts.basename_part.length), parts.basename_part.data,
      static_cast<int>(parts.basename_part.length), parts.basename_part.data);

  Path listing_directory =
      resolve_listing_directory(parts.directory_part, base_directory);

  Maybe<ArrayList<String>> entries = Path::read_directory(listing_directory);
  if (!entries.has_value()) return candidates;

  for (const String &entry : *entries) {
    if (!entry.view().starts_with(parts.basename_part)) continue;

    /* A name beginning with a dot stays hidden unless the user typed a leading
       dot, the way ls and the shell hide dotfiles. */
    if (entry.length() > 0 && entry.view()[0] == '.' &&
        (parts.basename_part.is_empty() || parts.basename_part[0] != '.'))
    {
      continue;
    }

    String candidate{parts.directory_part};
    candidate += entry.view();

    Path full = listing_directory;
    full.push_component(entry.view());
    if (full.is_directory()) candidate += '/';

    candidates.push(steal(candidate));
  }

  return candidates;
}

/* Resolve a glob token into its matches by listing the directory the fixed
   prefix names and keeping every entry that matches the pattern. The glob
   machinery walks one path component, so a multi-component glob falls back to a
   single directory listing here, which covers the common trailing-pattern
   case. */
static fn complete_glob(StringView token, const Path &base_directory) throws
    -> ArrayList<String>
{
  ArrayList<String> candidates{};

  path_token parts = split_path_token(token);

  TRACELN("resolving glob token '%.*s'", static_cast<int>(token.length),
          token.data);

  Path listing_directory =
      resolve_listing_directory(parts.directory_part, base_directory);

  Maybe<ArrayList<String>> entries = Path::read_directory(listing_directory);
  if (!entries.has_value()) return candidates;

  /* Every byte of the basename pattern is an active glob position, since the
     completion token is unquoted. */
  ArrayList<bool> glob_active{};
  glob_active.reserve(parts.basename_part.length);
  for (usize i = 0; i < parts.basename_part.length; i++)
    glob_active.push(true);

  for (const String &entry : *entries) {
    /* A dotfile only matches a pattern the user began with a dot, the way the
       shell hides them from a bare star. */
    if (entry.length() > 0 && entry.view()[0] == '.' &&
        (parts.basename_part.is_empty() || parts.basename_part[0] != '.'))
    {
      continue;
    }

    if (!utils::glob_matches(parts.basename_part, entry.view(), glob_active, 0))
    {
      continue;
    }

    String candidate{parts.directory_part};
    candidate += entry.view();

    Path full = listing_directory;
    full.push_component(entry.view());
    if (full.is_directory()) candidate += '/';

    candidates.push(steal(candidate));
  }

  return candidates;
}

fn complete(StringView line, usize cursor, EvalContext &context,
            const Path &base_directory) throws -> completion_result
{
  if (cursor > line.length) cursor = line.length;

  usize token_start = find_token_start(line, cursor);
  StringView token =
      line.substring_of_length(token_start, cursor - token_start);
  bool is_command = is_in_command_position(line, token_start);

  TRACELN("complete line '%.*s' cursor %zu token '%.*s' command %d",
          static_cast<int>(line.length), line.data, cursor,
          static_cast<int>(token.length), token.data, is_command ? 1 : 0);

  ArrayList<String> candidates{};

  if (token_has_glob_metacharacter(token)) {
    candidates = complete_glob(token, base_directory);
  } else if (is_command) {
    candidates = complete_command(token, context);
  } else {
    candidates = complete_filesystem(token, base_directory);
  }

  utils::sort_ascending(candidates);

  String longest_common_prefix = compute_longest_common_prefix(candidates);

  return completion_result{
      steal(candidates), steal(longest_common_prefix), token_start, cursor,
      is_command,
  };
}

} /* namespace completion */

} /* namespace shit */
