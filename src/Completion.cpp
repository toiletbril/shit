#include "Completion.hpp"

#include "Builtin.hpp"
#include "Debug.hpp"
#include "HashSet.hpp"
#include "Path.hpp"
#include "Platform.hpp"
#include "Tokens.hpp"
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

/* True for a byte that ends the token under the cursor, either a word separator
   or a command separator. A token bounded this way stops at an unspaced
   separator, so `ls|gre` ends the token at `|` and leaves `gre` under the
   cursor rather than the whole `ls|gre` run. */
static pure fn is_token_boundary(char c) wontthrow -> bool
{
  return is_word_separator(c) || is_command_separator(c);
}

/* The byte offset where the token under the cursor begins. The scan walks back
   from the cursor over non-boundary bytes. */
static pure fn find_token_start(StringView line, usize cursor) wontthrow
    -> usize
{
  usize start = cursor;
  while (start > 0 && !is_token_boundary(line[start - 1]))
    start--;
  return start;
}

/* The byte offset just past the token the cursor sits inside. The scan walks
   forward from the cursor over non-boundary bytes, so the replacement covers
   the whole word rather than the bytes left of the cursor only. */
static pure fn find_token_end(StringView line, usize cursor) wontthrow -> usize
{
  usize end = cursor;
  while (end < line.length && !is_token_boundary(line[end]))
    end++;
  return end;
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

/* A command-position token matches a command name either by carrying it as a
   plain prefix or, when the token holds a glob metacharacter, by matching it as
   a glob pattern. The shell offers command names rather than cwd entries for a
   bare glob first word, so a glob like ec* lists the commands it matches. */
static fn command_name_matches(StringView name, StringView token,
                               bool token_is_glob) throws -> bool
{
  if (!token_is_glob) return name.starts_with(token);

  ArrayList<bool> glob_active{};
  glob_active.reserve(token.length);
  for (usize i = 0; i < token.length; i++)
    glob_active.push(true);
  return utils::glob_matches(token, name, glob_active, 0);
}

/* Append name to candidates when it matches the typed token and the seen set
   has not recorded it, so the merged command list stays deduped across
   builtins, functions, aliases, and PATH. The seen set replaces the linear
   scan, so each insert costs one hash lookup rather than a walk of every
   candidate already collected. */
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
  String prefix{candidates[0].view()};
  for (usize i = 1; i < candidates.count(); i++) {
    const String &candidate = candidates[i];
    usize shared = 0;
    while (shared < prefix.length() && shared < candidate.length() &&
           prefix.view()[shared] == candidate.view()[shared])
    {
      shared++;
    }
    /* The byte-wise match can stop in the middle of a multibyte codepoint when
       two candidates agree on a lead byte but differ on a continuation byte, so
       retract the cut back to the start of that codepoint to avoid corrupting a
       glyph in the ghost text. A cut at the prefix end sits on a boundary
       already. */
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
static fn complete_command(StringView token, bool token_is_glob,
                           EvalContext &context) throws -> ArrayList<String>
{
  ArrayList<String> candidates{};
  HashSet seen{heap_allocator()};

  TRACELN("completing command position for token '%.*s'",
          static_cast<int>(token.length), token.data);

  for (const String &builtin_name : builtin_names()) {
    add_unique_command(candidates, seen, builtin_name.view(), token,
                       token_is_glob);
  }

  context.function_names().for_each([&](StringView name) {
    add_unique_command(candidates, seen, name, token, token_is_glob);
  });

  context.alias_names().for_each([&](StringView name) {
    add_unique_command(candidates, seen, name, token, token_is_glob);
  });

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
        add_unique_command(candidates, seen, entry.view(), token,
                           token_is_glob);
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

    /* Path::read_directory hands back names only, so the trailing slash needs a
       stat per entry. Threading the dirent d_type through read_directory would
       change its signature and the Windows path, so the stat stays. */
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

/* Whether the token names a variable expansion, a leading '$' the user is
   typing a name after. The completion offers variable names for it rather than
   files or commands. */
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
  ArrayList<String> candidates{};

  /* Strip the leading '$' and an optional '{', so the rest is the partial name.
     The brace form is reproduced on every candidate. */
  bool has_brace = token.length >= 2 && token[1] == '{';
  usize name_start = has_brace ? 2 : 1;
  StringView prefix = token.substring(name_start);

  TRACELN("completing variable token '%.*s', prefix '%.*s', brace %d",
          static_cast<int>(token.length), token.data,
          static_cast<int>(prefix.length), prefix.data, has_brace ? 1 : 0);

  HashSet seen{heap_allocator()};

  let add_name = [&](StringView name) throws -> void {
    if (!name.starts_with(prefix)) return;
    if (seen.contains(name)) return;
    seen.add(name);

    String candidate{};
    candidate += has_brace ? "${" : "$";
    candidate.append(name);
    if (has_brace) candidate.push('}');
    candidates.push(steal(candidate));
  };

  context.variable_names().for_each([&](StringView name) { add_name(name); });

  for (const String &name : os::environment_names())
    add_name(name.view());

  return candidates;
}

fn complete(StringView line, usize cursor, EvalContext &context,
            const Path &base_directory) throws -> completion_result
{
  if (cursor > line.length) cursor = line.length;

  /* The replaced span covers the whole word the cursor sits inside, from the
     token start to the token end, so a cursor in the middle of a word replaces
     the word cleanly rather than keeping the bytes to its right. */
  const usize token_start = find_token_start(line, cursor);
  const usize token_end = find_token_end(line, cursor);
  const StringView token =
      line.substring_of_length(token_start, token_end - token_start);
  const bool is_command = is_in_command_position(line, token_start);
  const bool token_is_glob = token_has_glob_metacharacter(token);

  /* A command-position token that names a path component is a program given by
     path rather than a bare command word, so it completes against the
     filesystem the way dash does instead of the command name sets. */
  const bool token_has_path_separator = token.find_character('/').has_value();

  TRACELN("complete line '%.*s' cursor %zu token '%.*s' command %d",
          static_cast<int>(line.length), line.data, cursor,
          static_cast<int>(token.length), token.data, is_command ? 1 : 0);

  ArrayList<String> candidates{};

  if (token_is_variable(token)) {
    candidates = complete_variable(token, context);
  } else if (is_command && !token_has_path_separator) {
    candidates = complete_command(token, token_is_glob, context);
  } else if (token_is_glob) {
    candidates = complete_glob(token, base_directory);
  } else {
    candidates = complete_filesystem(token, base_directory);
  }

  utils::sort_ascending(candidates);

  String longest_common_prefix = compute_longest_common_prefix(candidates);

  return completion_result{
      steal(candidates), steal(longest_common_prefix), token_start, token_end,
      is_command,
  };
}

/* Whether the byte begins a shell word, used to find the start of the first
   command word past any leading whitespace. */
static pure fn is_leading_whitespace(char c) wontthrow -> bool
{
  return c == ' ' || c == '\t';
}

/* Whether the first word names a runnable command. The word resolves when it is
   a reserved keyword the shell runs as syntax, a builtin, a function, an alias,
   a PATH executable, or, when it holds a slash, an existing regular file the
   process may execute. */
static fn first_word_resolves(StringView word, EvalContext &context) throws
    -> bool
{
  /* A reserved word is valid shell syntax rather than a command name, so it is
     never colored as unresolvable. */
  if (KEYWORDS.find(word).has_value()) return true;

  /* A path word resolves against the filesystem the way the evaluator does for
     a program given by path, rather than the command name sets. */
  if (word.find_character('/').has_value()) {
    if (Maybe<Path> canonical = utils::canonicalize_path(word);
        canonical.has_value())
    {
      return canonical->is_regular_file() && canonical->is_executable();
    }
    return false;
  }

  if (search_builtin(word).has_value()) return true;
  if (context.find_function(word) != nullptr) return true;
  if (context.get_alias(word).has_value()) return true;

  return utils::search_program_path(word).count() > 0;
}

fn highlight_first_word(StringView line, EvalContext &context) throws
    -> highlight_result
{
  highlight_result result{0, 0, false};

  usize word_start = 0;
  while (word_start < line.length && is_leading_whitespace(line[word_start]))
    word_start++;

  /* An empty line or one that opens a subshell or group has no command word to
     color, so it stays plain. */
  if (word_start >= line.length) return result;
  if (line[word_start] == '(') return result;

  usize word_end = word_start;
  while (word_end < line.length && !is_token_boundary(line[word_end]))
    word_end++;

  const StringView word =
      line.substring_of_length(word_start, word_end - word_start);

  /* A leading assignment such as VAR=value is not a command, so a '=' before any
     slash leaves the line plain the way the parser treats it as an assignment
     prefix. */
  for (usize i = 0; i < word.length; i++) {
    if (word[i] == '/') break;
    if (word[i] == '=') return result;
  }

  TRACELN("highlighting first word '%.*s'", static_cast<int>(word.length),
          word.data);

  result.word_start = word_start;
  result.word_end = word_end;
  result.should_color = !first_word_resolves(word, context);
  return result;
}

} /* namespace completion */

} /* namespace shit */
