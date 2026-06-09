#include "Completion.hpp"

#include "Builtin.hpp"
#include "Colors.hpp"
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
/* The keyword prefixes that are transparent to command position, so a command
   word can follow them: ! and time and time's -p and --posix options. */
static pure fn is_transparent_command_prefix(StringView word) wontthrow -> bool
{
  return word == "!" || word == "time" || word == "-p" || word == "--posix";
}

static pure fn is_in_command_position(StringView line,
                                      usize token_start) wontthrow -> bool
{
  usize i = token_start;
  for (;;) {
    while (i > 0 && is_word_separator(line[i - 1]))
      i--;
    if (i == 0) return true;
    if (is_command_separator(line[i - 1])) return true;
    /* The word right before is examined, and a transparent keyword prefix is
       stepped over so the word after time or ! is still a command word. */
    usize word_start = i;
    while (word_start > 0 && !is_word_separator(line[word_start - 1]) &&
           !is_command_separator(line[word_start - 1]))
      word_start--;
    if (!is_transparent_command_prefix(
            line.substring_of_length(word_start, i - word_start)))
      return false;
    i = word_start;
  }
}

/* A command-position token matches a command name either by carrying it as a
   plain prefix or, when the token holds a glob metacharacter, by matching it as
   a glob pattern. The shell offers command names rather than cwd entries for a
   bare glob first word, so a glob like ec* lists the commands it matches. */
static fn command_name_matches(StringView name, StringView token,
                               bool token_is_glob) throws -> bool
{
  if (!token_is_glob) return name.starts_with(token);

  let glob_active = ArrayList<bool>{};
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
  let prefix = String{candidates[0].view()};
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
/* The PATH executable names, cached so the per-keystroke ghost does not read
   every PATH directory on each keystroke. The cache rebuilds only when the PATH
   value changes, which an interactive session does rarely, so a freeze on a
   large PATH becomes a one-time cost rather than a per-key one. */
static String CACHED_COMPLETION_PATH{};
static ArrayList<String> CACHED_PATH_COMMANDS{};
static bool CACHED_PATH_COMMANDS_VALID = false;

static fn path_command_names() throws -> const ArrayList<String> &
{
  let const path = os::get_environment_variable("PATH");
  let const current = path.has_value() ? path->view() : StringView{};
  if (CACHED_PATH_COMMANDS_VALID && CACHED_COMPLETION_PATH.view() == current)
    return CACHED_PATH_COMMANDS;

  CACHED_PATH_COMMANDS.clear();
  usize segment_start = 0;
  for (usize i = 0; i <= current.length; i++) {
    if (i != current.length && current[i] != os::PATH_DELIMITER) continue;
    let const segment =
        current.substring_of_length(segment_start, i - segment_start);
    segment_start = i + 1;
    let const directory = Path{segment.is_empty() ? StringView{"."} : segment};
    if (Maybe<ArrayList<String>> entries = Path::read_directory(directory)) {
      for (String &entry : *entries)
        CACHED_PATH_COMMANDS.push(steal(entry));
    }
  }
  CACHED_COMPLETION_PATH = String{current};
  CACHED_PATH_COMMANDS_VALID = true;
  return CACHED_PATH_COMMANDS;
}

static fn complete_command(StringView token, bool token_is_glob,
                           EvalContext &context) throws -> ArrayList<String>
{
  let candidates = ArrayList<String>{};
  let seen = HashSet{heap_allocator()};

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

  for (const String &entry : path_command_names())
    add_unique_command(candidates, seen, entry.view(), token, token_is_glob);

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

  /* A leading tilde expands to a home directory, the current user's for a bare
     ~ or the named user's for ~user. The name runs from after the ~ to the
     first /. An unknown name falls through to the relative-path handling below,
     which leaves the tilde literal. */
  if (directory_part[0] == '~') {
    usize name_end = 1;
    while (name_end < directory_part.length && directory_part[name_end] != '/')
      name_end++;
    let const name = directory_part.substring_of_length(1, name_end - 1);
    Maybe<Path> home = name.is_empty() ? os::get_home_directory()
                                       : os::get_home_for_user(name);
    if (home.has_value()) {
      let resolved = home->clone();
      /* Drop the name and the separator after it, then append the rest. */
      usize rest_start = name_end;
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

  let const directory = Path{directory_part};
  if (directory.is_absolute()) return directory;

  let resolved = base_directory.clone();
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
  let candidates = ArrayList<String>{};

  path_token parts = split_path_token(token);

  TRACELN(
      "completing filesystem token '%.*s', dir '%.*s', base '%.*s'",
      static_cast<int>(token.length), token.data,
      static_cast<int>(parts.basename_part.length), parts.basename_part.data,
      static_cast<int>(parts.basename_part.length), parts.basename_part.data);

  let listing_directory =
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

    let candidate = String{parts.directory_part};
    candidate += entry.view();

    /* Path::read_directory hands back names only, so the trailing slash needs a
       stat per entry. Threading the dirent d_type through read_directory would
       change its signature and the Windows path, so the stat stays. */
    let full = listing_directory.clone();
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
  let candidates = ArrayList<String>{};

  path_token parts = split_path_token(token);

  TRACELN("resolving glob token '%.*s'", static_cast<int>(token.length),
          token.data);

  let listing_directory =
      resolve_listing_directory(parts.directory_part, base_directory);

  Maybe<ArrayList<String>> entries = Path::read_directory(listing_directory);
  if (!entries.has_value()) return candidates;

  /* Every byte of the basename pattern is an active glob position, since the
     completion token is unquoted. */
  let glob_active = ArrayList<bool>{};
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

    let candidate = String{parts.directory_part};
    candidate += entry.view();

    let full = listing_directory.clone();
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
  let candidates = ArrayList<String>{};

  /* Strip the leading '$' and an optional '{', so the rest is the partial name.
     The brace form is reproduced on every candidate. */
  bool has_brace = token.length >= 2 && token[1] == '{';
  usize name_start = has_brace ? 2 : 1;
  let const prefix = token.substring(name_start);

  TRACELN("completing variable token '%.*s', prefix '%.*s', brace %d",
          static_cast<int>(token.length), token.data,
          static_cast<int>(prefix.length), prefix.data, has_brace ? 1 : 0);

  let seen = HashSet{heap_allocator()};

  let add_name = [&](StringView name) throws -> void {
    if (!name.starts_with(prefix)) return;
    if (seen.contains(name)) return;
    seen.add(name);

    let candidate = String{};
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

/* A token that is a leading ~ with no / yet, such as ~ or ~ro, completes
   against the system user names. Once a / appears the directory part resolves
   the user's home and the filesystem handler takes over. */
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
  for (const String &user : os::enumerate_users()) {
    if (!user.view().starts_with(prefix)) continue;
    let candidate = String{};
    candidate.push('~');
    candidate.append(user.view());
    candidate.push('/');
    candidates.push(steal(candidate));
  }
  return candidates;
}

/* The command a registered completion spec is looked up by, the first
   whitespace-delimited word of the line past any transparent keyword prefix such
   as time or ! so the spec for the real command is found. */
static fn command_word_of(StringView line) wontthrow -> StringView
{
  usize i = 0;
  for (;;) {
    while (i < line.length && (line[i] == ' ' || line[i] == '\t'))
      i++;
    const usize start = i;
    while (i < line.length && line[i] != ' ' && line[i] != '\t')
      i++;
    const StringView word = line.substring_of_length(start, i - start);
    if (word.is_empty() || !is_transparent_command_prefix(word)) return word;
  }
}

/* Split the line into whitespace words for COMP_WORDS, reporting the index of
   the word the cursor sits in. An empty trailing word is appended when the
   cursor is past the last one, so the function reads an empty current word. */
static fn split_completion_words(StringView line, usize cursor,
                                 usize &cword) throws -> ArrayList<String>
{
  let words = ArrayList<String>{};
  usize i = 0;
  bool found = false;
  while (i < line.length) {
    while (i < line.length && (line[i] == ' ' || line[i] == '\t'))
      i++;
    if (i >= line.length) break;
    const usize start = i;
    while (i < line.length && line[i] != ' ' && line[i] != '\t')
      i++;
    if (cursor >= start && cursor <= i) {
      cword = words.count();
      found = true;
    }
    words.push(String{line.substring_of_length(start, i - start)});
  }
  if (!found) {
    cword = words.count();
    words.push(String{});
  }
  return words;
}

/* Consult the completion spec registered for the line's command, when one
   exists. The word list filters to the entries that start with the token, and
   the -F function runs only on an explicit tab so the ghost does not run it on
   every keystroke. None means no spec applied, so the caller completes
   filenames, which is also the result when a -o default spec found nothing. */
static fn complete_from_spec(StringView line, StringView token, usize cursor,
                             bool for_listing, EvalContext &context) throws
    -> Maybe<ArrayList<String>>
{
  const StringView command = command_word_of(line);
  if (command.is_empty()) return None;
  const completion_spec *spec = context.lookup_completion_spec(command);
  if (spec == nullptr) return None;

  let candidates = ArrayList<String>{};

  if (!spec->word_list.is_empty()) {
    const StringView list = spec->word_list.view();
    usize start = 0;
    for (usize i = 0; i <= list.length; i++) {
      const char c = i < list.length ? list[i] : ' ';
      if (c == ' ' || c == '\t' || c == '\n') {
        if (i > start) {
          const StringView word = list.substring_of_length(start, i - start);
          if (token.is_empty() || word.starts_with(token))
            candidates.push(String{word});
        }
        start = i + 1;
      }
    }
  }

  /* The function returns the final candidate list in COMPREPLY, already filtered
     to the current word, so its entries are taken as they are. */
  if (for_listing && !spec->function_name.is_empty()) {
    usize cword = 0;
    let const words = split_completion_words(line, cursor, cword);
    let const reply = context.run_completion_function(spec->function_name.view(),
                                                      words, cword, line, cursor);
    for (const String &entry : reply)
      candidates.push(String{heap_allocator(), entry.view()});
  }

  if (candidates.is_empty() && spec->use_default) return None;
  return candidates;
}

flatten fn complete(StringView line, usize cursor, EvalContext &context,
                    const Path &base_directory, bool for_listing) throws
    -> completion_result
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

  /* A glob word with the cursor right after it expands inline to its file
     matches, even in command position, the way the shell expands it before
     running a command. */
  const bool inline_glob = token_is_glob && cursor == token_end;

  let candidates = ArrayList<String>{};

  if (token_is_variable(token)) {
    candidates = complete_variable(token, context);
  } else if (token_is_tilde_user_prefix(token)) {
    candidates = complete_tilde_user(token);
  } else if (inline_glob) {
    candidates = complete_glob(token, base_directory);
    if (!candidates.is_empty()) {
      /* The matches replace the pattern as one space-joined run of fields. The
         trailing slash a directory match carries for the listing UI is dropped
         so the expansion reads as plain names. */
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
    /* An empty command token, the state right after a ; or a space in command
       position, would enumerate and sort every command in PATH on each
       keystroke for the ghost, which freezes the prompt on a large PATH and
       suggests nothing. Command completion runs only once a prefix is typed.
       An explicit tab on an empty word does list every command, since the user
       asked for the menu rather than a single suggestion. */
    if (!token.is_empty() || for_listing)
      candidates = complete_command(token, token_is_glob, context);
  } else if (token_is_glob) {
    candidates = complete_glob(token, base_directory);
  } else {
    /* An argument to a command that registered a completion spec consults the
       spec first, the way bash runs a programmable completion, and falls back to
       filenames when no spec applies or a -o default spec found nothing. */
    if (Maybe<ArrayList<String>> from_spec =
            complete_from_spec(line, token, cursor, for_listing, context);
        from_spec.has_value())
      candidates = steal(*from_spec);
    else
      candidates = complete_filesystem(token, base_directory);
  }

  /* A token that matched nothing skips the sort and the prefix scan, the common
     case while typing a novel word, so a keystroke that yields no candidate
     costs nothing past the lookup. */
  let longest_common_prefix = String{};
  if (!candidates.is_empty()) {
    utils::sort_ascending(candidates);
    longest_common_prefix = compute_longest_common_prefix(candidates);
  }

  return completion_result{
      steal(candidates), steal(longest_common_prefix), token_start, token_end,
      is_command,
  };
}

/* Whether the first word names a runnable command. The word resolves when it is
   a reserved keyword the shell runs as syntax, a builtin, a function, an alias,
   a PATH executable, or, when it holds a slash, an existing regular file the
   process may execute. */
/* Expand a leading tilde in a command path the way the evaluator does before it
   resolves the command, so ~/bin/foo is checked at the home directory rather
   than as a literal ~ path. None when the path names a user with no home. */
static fn expand_command_tilde(StringView word) throws -> Maybe<String>
{
  if (word.is_empty() || word[0] != '~') return None;
  let const slash = word.find_character('/');
  let const user = slash.has_value() ? word.substring_of_length(1, *slash - 1)
                                     : word.substring(1);
  Maybe<Path> home =
      user.is_empty() ? os::get_home_directory() : os::get_home_for_user(user);
  if (!home.has_value()) return None;
  let expanded = home->clone();
  if (slash.has_value()) expanded.push_component(word.substring(*slash + 1));
  return String{expanded.text().view()};
}

static fn first_word_resolves(StringView word, EvalContext &context) throws
    -> bool
{
  /* A reserved word is valid shell syntax rather than a command name, so it is
     never colored as unresolvable. The ! negation and the time keyword lead a
     command rather than name one, so they resolve the same way. */
  if (word == "!" || word == "time") return true;
  if (KEYWORDS.find(word).has_value()) return true;

  /* A path word resolves against the filesystem the way the evaluator does for
     a program given by path, rather than the command name sets. A leading tilde
     is expanded first, so ~/bin/foo is checked at the home directory. */
  if (word.find_character('/').has_value()) {
    let expanded = String{word};
    if (!word.is_empty() && word[0] == '~') {
      if (Maybe<String> home_expanded = expand_command_tilde(word))
        expanded = steal(*home_expanded);
      else
        return false;
    }
    /* An existing regular file resolves, even when it is not executable, since
       the name is found rather than missing and a process substitution such as
       <(/etc/profile) names a real file the highlighter should not flag. A
       non-executable file is a runtime permission matter, not a not-found one.
     */
    if (Maybe<Path> canonical = utils::canonicalize_path(expanded.view());
        canonical.has_value())
    {
      return canonical->is_regular_file() || canonical->is_directory();
    }
    return false;
  }

  if (search_builtin(word).has_value()) return true;
  if (context.find_function(word) != nullptr) return true;
  if (context.get_alias(word).has_value()) return true;

  return utils::search_program_path(word).count() > 0;
}

static pure fn is_highlight_name_start(char c) wontthrow -> bool
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static pure fn is_highlight_name_char(char c) wontthrow -> bool
{
  return is_highlight_name_start(c) || (c >= '0' && c <= '9');
}

/* A word that is a single plain identifier, the form a for loop variable must
   take. A leading digit or any non-name character fails the check. */
static pure fn is_plain_identifier(StringView word) wontthrow -> bool
{
  if (word.length == 0 || !is_highlight_name_start(word[0])) return false;
  for (usize i = 1; i < word.length; i++)
    if (!is_highlight_name_char(word[i])) return false;
  return true;
}

/* A byte that ends a top-level word, whitespace or an operator the scanner
   stops on. '{' and '}' are left out so a brace word such as a{1,2} stays one
   word. */
static pure fn is_highlight_word_break(char c) wontthrow -> bool
{
  return c == ' ' || c == '\t' || c == '\n' || c == '|' || c == '&' ||
         c == ';' || c == '<' || c == '>' || c == '(' || c == ')';
}

/* The byte just past a $ expansion that begins at dollar and stays within end,
   covering $name, the special and positional parameters, and ${...} with
   balanced braces. The $(...) form is handled by the caller so it can recurse
   into the inner command. */
static pure fn scan_dollar_expansion(StringView line, usize dollar,
                                     usize end) wontthrow -> usize
{
  usize i = dollar + 1;
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
  if (c == '?' || c == '!' || c == '#' || c == '$' || c == '*' || c == '@' ||
      c == '-')
  {
    return i + 1;
  }
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
     lexer keeps arr[i]=v one assignment word, so the highlighter keeps the next
     word in command position rather than coloring the array name as a command. */
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
    if (i + 1 < word.length && word[i] == '+' && word[i + 1] == '=') return true;
  }
  return false;
}

/* The open shell constructs, so a continuation or closing keyword can be
   checked against the construct it belongs to. */
enum class highlight_construct : u8
{
  If,
  WhileUntil,
  For,
  Case,
  Function,
};

static fn scan_highlight_range(StringView line, usize begin, usize end,
                               EvalContext &context,
                               ArrayList<highlight_span> &spans,
                               const HashSet &known_vars) throws -> void;

/* The plain variable name a $ expansion references when it is a simple $name or
   ${name}, None when the expansion carries an operator such as ${x:-y} or a
   form like ${#x} that is not a bare reference. The name may still be a special
   parameter or a positional, which the set check treats as always set. */
static fn simple_dollar_name(StringView line, usize i,
                             usize expansion_end) wontthrow -> Maybe<StringView>
{
  if (i + 1 >= expansion_end) return shit::None;
  if (line[i + 1] == '{') {
    if (expansion_end < i + 3 || line[expansion_end - 1] != '}')
      return shit::None;
    StringView inner =
        line.substring_of_length(i + 2, expansion_end - (i + 2) - 1);
    if (inner.is_empty()) return shit::None;
    for (usize k = 0; k < inner.length; k++)
      if (!is_highlight_name_char(inner[k])) return shit::None;
    return inner;
  }
  return line.substring_of_length(i + 1, expansion_end - (i + 1));
}

/* Whether a $ variable reference names something that is set, read without any
   side effect so the highlighter never advances RANDOM or reads the clock. A
   special parameter, a positional, a shell variable, an environment variable,
   and a synthesized dynamic variable all count as set. */
static fn dollar_name_is_set(StringView name, const HashSet &known_vars) throws
    -> bool
{
  if (name.is_empty()) return true;
  if (name.length == 1 && !is_highlight_name_start(name[0])) return true;

  bool all_digits = true;
  for (usize k = 0; k < name.length; k++)
    if (name[k] < '0' || name[k] > '9') {
      all_digits = false;
      break;
    }
  if (all_digits) return true;

  if (known_vars.contains(name)) return true;
  return os::get_environment_variable(name).has_value();
}

/* Color a $ expansion that begins at i within the window. A $(...) recurses so
   its inner command line colors like any other, while ${...}, $name, and the
   special parameters are colored cyan as one span, or bold red when the named
   variable is not set. Returns the index past it. */
static fn color_dollar(StringView line, usize i, usize end,
                       ArrayList<highlight_span> &spans, EvalContext &context,
                       const HashSet &known_vars) throws -> usize
{
  if (i + 1 < end && line[i + 1] == '(') {
    usize depth = 0;
    usize close = end;
    usize j = i + 1;
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
    /* The bytes between the ( and the ) are the inner command line, which the
       $( and ) frame in the default color. */
    let const inner_begin = i + 2 < end ? i + 2 : end;
    let const inner_end = close < inner_begin ? inner_begin : close;
    scan_highlight_range(line, inner_begin, inner_end, context, spans,
                         known_vars);
    return j;
  }
  let const expansion_end = scan_dollar_expansion(line, i, end);
  if (expansion_end > i) {
    StringView sgr = colors::ansi::CYAN;
    if (Maybe<StringView> name = simple_dollar_name(line, i, expansion_end);
        name.has_value() && !dollar_name_is_set(*name, known_vars))
      sgr = colors::ansi::BOLD_RED;
    spans.push(highlight_span{i, expansion_end, sgr});
  }
  return expansion_end;
}

/* Color one command line, the window [begin, end). A command substitution
   recurses through color_dollar with its own command-position and construct
   state, so a nested command line colors on its own. */
static fn scan_highlight_range(StringView line, usize begin, usize end,
                               EvalContext &context,
                               ArrayList<highlight_span> &spans,
                               const HashSet &known_vars) throws -> void
{
  let push = [&](usize start, usize stop, StringView sgr) throws -> void {
    if (start < stop) spans.push(highlight_span{start, stop, sgr});
  };

  let stack = ArrayList<highlight_construct>{};
  bool command_position = true;
  bool expecting_in = false;
  bool for_variable_pending = false;
  bool for_do_expected = false;

  usize i = begin;
  while (i < end) {
    let const c = line[i];

    if (c == ' ' || c == '\t' || c == '\n') {
      i++;
      continue;
    }

    if (c == '#') {
      push(i, end, colors::ansi::DIM);
      break;
    }

    /* An operator run, left in the default color. A separator or an opener
       moves the next word back to command position, a redirection does not. */
    if (c == '|' || c == '&' || c == ';' || c == '<' || c == '>' || c == '(' ||
        c == ')' || c == '{' || c == '}')
    {
      bool has_separator = false;
      bool has_redirect = false;
      bool has_opener = false;
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
          i++;
          break;
        } else {
          break;
        }
      }
      if (has_opener || (has_separator && !has_redirect)) {
        command_position = true;
        expecting_in = false;
      }
      continue;
    }

    /* A word, scanned to its break. Its quoted strings and expansions are
       colored into word_spans, which stand for an argument or an
       expansion-built command. A plain command or keyword word is colored
       whole below. */
    let const word_start = i;
    let word_spans = ArrayList<highlight_span>{};
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
        usize literal_start = i - 1;
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
        /* A backtick substitution recurses the same way $(...) does. */
        let const inner_begin = i + 1;
        i++;
        while (i < end && line[i] != '`')
          i++;
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

    /* A for or case awaits its in, which is the keyword there. */
    if (expecting_in && plain && word == "in") {
      push(word_start, word_end, colors::ansi::GREEN);
      expecting_in = false;
      for_variable_pending = false;
      command_position = false;
      /* A for loop names its word list after in and then requires do, with no
         command list between, so do is expected once the words end. A case
         takes patterns after in, so this only arms for a for. */
      if (!stack.is_empty() && stack.back() == highlight_construct::For)
        for_do_expected = true;
      continue;
    }

    /* The word right after for is the loop variable, which must be a plain
       identifier. An expansion, a quoted word, or a non-identifier here is
       rejected the way the parser rejects for $f, so it is shown in red rather
       than colored as an ordinary variable. */
    if (for_variable_pending) {
      for_variable_pending = false;
      command_position = false;
      if (!plain || !is_plain_identifier(word))
        push(word_start, word_end, colors::ansi::BOLD_RED);
      continue;
    }

    /* A for loop requires do once its word list ends, so the first command in
       that position other than do is misplaced and shown in red. The words of
       the list are not in command position, so they keep the flag set until the
       do position is reached, where do itself falls through to the keyword
       handling below and is colored green. */
    if (for_do_expected && command_position) {
      for_do_expected = false;
      if (word != "do") {
        push(word_start, word_end, colors::ansi::BOLD_RED);
        command_position = false;
        continue;
      }
    }

    if (command_position && plain && !is_assignment) {
      bool is_keyword = true;
      bool keyword_ok = true;
      bool next_is_command = true;
      bool opens_in = false;
      bool opens_for_variable = false;
      if (word == "if") {
        stack.push(highlight_construct::If);
      } else if (word == "while" || word == "until") {
        stack.push(highlight_construct::WhileUntil);
      } else if (word == "for") {
        stack.push(highlight_construct::For);
        next_is_command = false;
        opens_in = true;
        opens_for_variable = true;
      } else if (word == "case") {
        stack.push(highlight_construct::Case);
        next_is_command = false;
        opens_in = true;
      } else if (word == "function") {
        stack.push(highlight_construct::Function);
        next_is_command = false;
      } else if (word == "then") {
        keyword_ok =
            !stack.is_empty() && stack.back() == highlight_construct::If;
      } else if (word == "do") {
        keyword_ok = !stack.is_empty() &&
                     (stack.back() == highlight_construct::WhileUntil ||
                      stack.back() == highlight_construct::For);
      } else if (word == "else" || word == "elif") {
        keyword_ok =
            !stack.is_empty() && stack.back() == highlight_construct::If;
      } else if (word == "fi") {
        keyword_ok =
            !stack.is_empty() && stack.back() == highlight_construct::If;
        if (keyword_ok) stack.pop_back();
      } else if (word == "done") {
        keyword_ok = !stack.is_empty() &&
                     (stack.back() == highlight_construct::WhileUntil ||
                      stack.back() == highlight_construct::For);
        if (keyword_ok) stack.pop_back();
      } else if (word == "esac") {
        keyword_ok =
            !stack.is_empty() && stack.back() == highlight_construct::Case;
        if (keyword_ok) stack.pop_back();
      } else if (word == "time" || word == "when") {
        /* A prefix keyword, the command follows it. */
      } else if (word == "in") {
        /* An in outside a for or case is misplaced. */
        keyword_ok = false;
        next_is_command = false;
      } else {
        is_keyword = false;
      }

      if (is_keyword) {
        push(word_start, word_end,
             keyword_ok ? colors::ansi::GREEN : colors::ansi::BOLD_RED);
        command_position = next_is_command;
        if (opens_in) expecting_in = true;
        if (opens_for_variable) for_variable_pending = true;
        continue;
      }

      /* A command name. A resolved command keeps the default color, an
         unresolved one is red. */
      if (!first_word_resolves(word, context))
        push(word_start, word_end, colors::ansi::RED);
      command_position = false;
      continue;
    }

    /* An argument, an expansion-built command, or an assignment prefix. The
       inner spans stand. An assignment prefix keeps the next word in command
       position, an expansion-built command moves past it. */
    for (const highlight_span &inner : word_spans)
      push(inner.start, inner.end, inner.sgr);
    if (command_position && !is_assignment) command_position = false;
  }
}

/* The variable names the line itself introduces, a for or select loop variable
   and a plain NAME= assignment, so the highlighter does not red a reference to a
   name the same line binds. */
static fn add_line_bound_variables(StringView line, HashSet &known_vars) throws
    -> void
{
  let const is_separator = [](char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == ';' || c == '|' ||
           c == '&' || c == '(' || c == ')';
  };
  let const is_identifier = [](StringView name) {
    if (name.is_empty() || !is_highlight_name_start(name[0])) return false;
    for (usize i = 1; i < name.length; i++)
      if (!is_highlight_name_char(name[i])) return false;
    return true;
  };

  bool bind_next = false;
  usize i = 0;
  while (i < line.length) {
    while (i < line.length && is_separator(line[i]))
      i++;
    const usize start = i;
    while (i < line.length && !is_separator(line[i]))
      i++;
    if (i == start) break;
    const StringView token = line.substring_of_length(start, i - start);

    if (bind_next) {
      if (is_identifier(token)) known_vars.add(token);
      bind_next = false;
    } else if (Maybe<usize> equals = token.find_character('=');
               equals.has_value() && equals.value() > 0)
    {
      StringView name = token.substring_of_length(0, equals.value());
      /* An array-element assignment binds the base name, so arr[0]=1 makes arr
         known rather than the invalid name arr[0. */
      if (Maybe<usize> bracket = name.find_character('[');
          bracket.has_value())
        name = name.substring_of_length(0, bracket.value());
      if (is_identifier(name)) known_vars.add(name);
    }
    bind_next = token == "for" || token == "select";
  }
}

fn highlight_line(StringView line, EvalContext &context) throws
    -> ArrayList<highlight_span>
{
  let spans = ArrayList<highlight_span>{};
  /* The set of named variables is read once per line so the per-expansion check
     does no allocation and triggers no dynamic-variable side effect. A line
     with no $ never references a variable, so the whole walk over the variable
     store is skipped on the common plain-command keystroke. */
  let known_vars = HashSet{heap_allocator()};
  if (line.find_character('$').has_value()) {
    known_vars = context.variable_names();
    /* The variables the evaluator synthesizes on read are not in the store, so
       they are added here as set rather than computed, which would advance
       RANDOM or read the clock on a keystroke. IFS and LINENO exist in every
       mode, while the rest are bash-mode only, the way get_variable_value gates
       them, so a POSIX run reds an unset $RANDOM. */
    known_vars.add(StringView{"IFS"});
    known_vars.add(StringView{"LINENO"});
    if (context.is_bash_compatible()) {
      known_vars.add(StringView{"RANDOM"});
      known_vars.add(StringView{"SECONDS"});
      known_vars.add(StringView{"EPOCHSECONDS"});
      known_vars.add(StringView{"EPOCHREALTIME"});
      known_vars.add(StringView{"BASHPID"});
      known_vars.add(StringView{"BASH_SUBSHELL"});
      known_vars.add(StringView{"BASH_SOURCE"});
    }
    add_line_bound_variables(line, known_vars);
  }
  scan_highlight_range(line, 0, line.length, context, spans, known_vars);
  return spans;
}

} /* namespace completion */

} /* namespace shit */
