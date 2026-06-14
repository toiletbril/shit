#include "Completion.hpp"

#include "Arena.hpp"
#include "Builtin.hpp"
#include "Colors.hpp"
#include "Debug.hpp"
#include "HashSet.hpp"
#include "Lexer.hpp"
#include "Path.hpp"
#include "Platform.hpp"
#include "Tokens.hpp"
#include "Trace.hpp"
#include "Utils.hpp"

namespace shit {

namespace completion {

/* The syntax highlighter rebuilds its spans, its per-word and per-construct
   lists, and the set of known variable names on every keystroke, all of which
   die when the next keystroke redraws. They live on this arena, reset at the
   top of highlight_line so the previous render's spans stay valid until the
   line editor has drained them into its own buffer. */
static BumpArena HIGHLIGHT_ARENA{};

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

/* Whether the closing paren at position matches no opener earlier on the
   line, the shape of a case pattern's closing paren, where the word after it
   opens the arm's first command. A matched paren closes a subshell or a
   substitution instead, and the word after that one is an argument. */
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
  let start = cursor;
  while (start > 0 && !is_token_boundary(line[start - 1]))
    start--;
  return start;
}

/* The byte offset just past the token the cursor sits inside. The scan walks
   forward from the cursor over non-boundary bytes, so the replacement covers
   the whole word rather than the bytes left of the cursor only. */
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
/* The leading words that are transparent to command position, so a command
   word can follow them: the ! and time keywords, the compound keywords whose
   body opens with a command, if and while and their kin, and the wrapper
   commands whose argument is itself a command, the way fish skips sudo. A
   dash word is a wrapper's own option and an =-carrying word a leading
   assignment, so sudo -E ls and FOO=bar make both read the inner command.
   for and case stay opaque since a variable or a subject word follows them,
   and in stays opaque since patterns or operands follow it. */
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
    return true;
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
    /* A case pattern's closing paren opens the arm's body, so case $x in a)
       completes a command there the way the byte after do does. */
    if (line[i - 1] == ')' && is_unmatched_closing_paren(line, i - 1))
      return true;
    /* The word right before is examined, and a transparent keyword prefix is
       stepped over so the word after time or ! is still a command word. */
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

/* True when the byte is a space or a tab, the blanks the --help and the manpage
   parsers step over between an option and its description. */
static pure forceinline fn is_blank(char byte) throws -> bool
{
  return byte == ' ' || byte == '\t';
}

/* The first index at or after `from` whose byte is not a blank. */
static pure forceinline fn skip_blanks(StringView text, usize from) throws
    -> usize
{
  while (from < text.length && is_blank(text[from]))
    from++;
  return from;
}

/* The view with its leading and trailing blanks removed. */
static pure forceinline fn trim_blanks(StringView text) throws -> StringView
{
  let const start = skip_blanks(text, 0);
  let end = text.length;
  while (end > start && is_blank(text[end - 1]))
    end--;
  return text.substring_of_length(start, end - start);
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

/* A command-position token matches a command name either by carrying it as a
   plain prefix or, when the token holds a glob metacharacter, by matching it as
   a glob pattern. The shell offers command names rather than cwd entries for a
   bare glob first word, so a glob like ec* lists the commands it matches. */
static fn command_name_matches(StringView name, StringView token,
                               bool token_is_glob) throws -> bool
{
  if (!token_is_glob) return name.starts_with(token);

  let const glob_active = all_active_glob_mask(token.length);
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

/* Whether the live PATH differs from the cached copy, updating the copy on a
   change. The probe runs on every keystroke, so the value is read as a raw
   view with no owning copy. */
static fn environment_path_changed(String &cached_path) throws -> bool
{
  const char *path = std::getenv("PATH");
  let const current = path != nullptr ? StringView{path} : StringView{};
  if (cached_path.view() == current) return false;
  cached_path = String{current};
  return true;
}

static fn path_command_names() throws -> const ArrayList<String> &
{
  if (!environment_path_changed(CACHED_COMPLETION_PATH) &&
      CACHED_PATH_COMMANDS_VALID)
    return CACHED_PATH_COMMANDS;

  /* The helper above already stored the live PATH value, so the rebuild
     walks the cached copy. A directory repeated in PATH is read only once,
     since a later occurrence lists the same commands, so a layered profile
     does not multiply the scan. */
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
      for (String &entry : *entries)
        CACHED_PATH_COMMANDS.push(steal(entry));
    }
  }
  CACHED_PATH_COMMANDS_VALID = true;
  LOG(verbosity::Info, "rebuilt the path command cache, %zu names",
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

  LOG(verbosity::All, "collected %zu command candidates for token '%.*s'",
      candidates.count(), static_cast<int>(token.length), token.data);

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

  /* A leading tilde expands to a home directory, the current user's for a bare
     ~ or the named user's for ~user. The name runs from after the ~ to the
     first /. An unknown name falls through to the relative-path handling below,
     which leaves the tilde literal. */
  if (directory_part[0] == '~') {
    usize name_end = 1;
    while (name_end < directory_part.length && directory_part[name_end] != '/')
      name_end++;
    let const name = directory_part.substring_of_length(1, name_end - 1);
    let home = name.is_empty() ? os::get_home_directory()
                               : os::get_home_for_user(name);
    if (home.has_value()) {
      let resolved = home->clone();
      /* Drop the name and the separator after it, then append the rest. */
      let rest_start = name_end;
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

  let entries = Path::read_directory(listing_directory);
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

  LOG(verbosity::All, "%zu entries of '%s' match basename '%.*s'",
      candidates.count(), listing_directory.text().c_str(),
      static_cast<int>(parts.basename_part.length), parts.basename_part.data);

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

  let entries = Path::read_directory(listing_directory);
  if (!entries.has_value()) return candidates;

  /* Every byte of the basename pattern is an active glob position, since the
     completion token is unquoted. */
  let const glob_active = all_active_glob_mask(parts.basename_part.length);

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

  LOG(verbosity::All, "glob pattern '%.*s' matched %zu entries",
      static_cast<int>(token.length), token.data, candidates.count());

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
  let has_brace = token.length >= 2 && token[1] == '{';
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

  LOG(verbosity::All, "%zu variable names match prefix '%.*s'",
      candidates.count(), static_cast<int>(prefix.length), prefix.data);

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
  LOG(verbosity::All, "%zu user names match tilde prefix '%.*s'",
      candidates.count(), static_cast<int>(prefix.length), prefix.data);
  return candidates;
}

/* The command a registered completion spec is looked up by, the first
   whitespace-delimited word of the line's last command segment past any
   transparent keyword prefix such as time or !, so echo hi; git - completes
   against git rather than echo. The separator scan is unquoted, the same
   lightweight reading the rest of the engine applies to a half-typed line. */
static fn command_word_of(StringView line) wontthrow -> StringView
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

/* The command a spec or manpage lookup reads after following an alias to its
   first word and a PATH symlink to its target basename, so completing g for a
   g='git' alias consults git, and a vi that links to nvim reads nvim's
   options. The alias walk is bounded against a cycle, and a name that
   resolves to nothing returns unchanged. */
/* Expand a command name through alias definitions only, the first word of each
   expansion, bounded so a cyclic alias terminates. Symlinks are left alone, so
   a name that dispatches on its argv[0] such as a busybox or rustup link keeps
   the surface name the user typed, which the --help fork runs so the
   multiplexer answers as that name. */
static fn resolve_completion_alias(StringView command,
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

static fn resolve_completion_command(StringView command,
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
   the word the cursor sits in. An empty trailing word is appended when the
   cursor is past the last one, so the function reads an empty current word. */
static fn split_completion_words(StringView line, usize cursor,
                                 usize &cword) throws -> ArrayList<String>
{
  let words = ArrayList<String>{};
  usize i = 0;
  let found = false;
  while (i < line.length) {
    while (i < line.length && (line[i] == ' ' || line[i] == '\t'))
      i++;
    if (i >= line.length) break;
    let const start = i;
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

/* A name a man page or a --help text lists with the description printed beside
   it. The completion menu shows the description dimmed after the name. */
struct help_entry
{
  String name;
  String description;
};

/* Keeps the entries whose name opens with the token, returns their names, and
   carries each kept entry's description into the menu's descriptions map. The
   option and subcommand stages share this since they all match a help_entry
   list against the token the user has typed. */
static fn matches_from_help_entries(const ArrayList<help_entry> &entries,
                                    StringView token,
                                    StringMap<String> &descriptions) throws
    -> ArrayList<String>
{
  let matches = ArrayList<String>{};
  for (const help_entry &entry : entries)
    if (entry.name.view().starts_with(token)) {
      matches.push(String{entry.name.view()});
      if (!entry.description.is_empty())
        descriptions.set(entry.name.view(), String{entry.description.view()});
    }
  return matches;
}

/* The options parsed out of a manpage with their descriptions, cached per
   command so a second tab on the same command pays no man fork. An empty list
   is cached too, so a command with no manpage or no options is not retried. */
static StringMap<ArrayList<help_entry>> MANPAGE_OPTION_CACHE{heap_allocator()};

/* The shared-manpage aliases, the only commands whose options live under a
   different page name. A general scan of `man COMMAND` covers every other
   command, so the table stays tiny and is not a list of every known tool. */
static fn manpage_name_for(StringView command) throws -> String
{
  if (command == "clang++" || command == "c++") return String{"clang"};
  if (command == "g++") return String{"gcc"};
  return String{command};
}

/* The subcommand index scanned out of the man1 directories, a command name
   mapped to the subcommands its dashed pages document, so git-commit.1 makes
   commit a candidate for git with no per-program table. The scan is a readdir
   pass that runs once per launch on the first explicit tab, never on the
   ghost path and never at startup. */
static StringMap<ArrayList<String>> MAN_SUBCOMMAND_INDEX{heap_allocator()};
/* Every stripped section-1 page name mapped to its full file path. The key
   set is the existence gate for the subcommand split and the lookup that
   sends git commit -<tab> to the git-commit page, and the path lets the
   synopsis validation read the page source directly instead of forking man
   per candidate. */
static StringMap<String> MAN_PAGE_FILE_PATHS{heap_allocator()};
/* The synopsis verdict for each command-subcommand page, keyed by the page
   name, so a page is read at most once per launch and only when the token
   matches its subcommand. */
static StringMap<bool> MAN_SUBCOMMAND_PAGE_VALID{heap_allocator()};
static bool MAN_SUBCOMMAND_INDEX_IS_BUILT = false;

/* The man1 directories of the host, the $MANPATH entries when the variable is
   set and the stock /usr/local and /usr trees otherwise. An empty $MANPATH
   segment, a leading or trailing colon or a doubled one, stands for the
   system defaults at that position, the manpath(1) reading, so a profile that
   appends `:$extra` keeps the stock pages. A directory that does not exist
   contributes nothing to the readdir pass. */
static fn manpage_section1_directories() throws -> ArrayList<Path>
{
  let directories = ArrayList<Path>{};
  let seen_roots = HashSet{heap_allocator()};

  auto push_man1_of_root = [&](StringView root) {
    if (seen_roots.contains(root)) return;
    seen_roots.add(root);
    let directory = Path{root};
    directory.push_component("man1");
    directories.push(steal(directory));
  };
  auto push_default_roots = [&]() {
    push_man1_of_root("/usr/local/share/man");
    push_man1_of_root("/usr/share/man");
  };

  let const manpath = os::get_environment_variable("MANPATH");
  if (!manpath.has_value() || manpath->is_empty()) {
    push_default_roots();
    return directories;
  }

  let const value = manpath->view();
  usize segment_start = 0;
  for (usize i = 0; i <= value.length; i++) {
    if (i != value.length && value[i] != os::PATH_DELIMITER) continue;
    let const segment =
        value.substring_of_length(segment_start, i - segment_start);
    segment_start = i + 1;
    if (segment.is_empty())
      push_default_roots();
    else
      push_man1_of_root(segment);
  }
  return directories;
}

/* The page name with its .1 section suffix and an optional compression tail
   removed, None when the entry is not a section-1 page. */
static pure fn strip_man1_suffix(StringView entry) wontthrow
    -> Maybe<StringView>
{
  let name = entry;
  for (const StringView tail : {StringView{".gz"}, StringView{".xz"},
                                StringView{".zst"}, StringView{".bz2"}})
  {
    if (name.length > tail.length &&
        name.substring(name.length - tail.length) == tail)
    {
      name = name.substring_of_length(0, name.length - tail.length);
      break;
    }
  }
  if (name.length > 2 && name.substring(name.length - 2) == ".1")
    return name.substring_of_length(0, name.length - 2);
  return None;
}

/* One readdir pass over the man1 directories builds the page-name set, then a
   second pass over the collected names splits each dashed page at its first
   dash. The tail survives as a subcommand only when the head page exists too,
   so xdg-open invents no xdg command, and a digit-leading tail such as the
   aclocal-1.16 version suffix is no subcommand either. */
static fn build_man_subcommand_index() throws -> void
{
  MAN_SUBCOMMAND_INDEX_IS_BUILT = true;
  for (const Path &directory : manpage_section1_directories()) {
    LOG(verbosity::Info, "scanning man1 directory '%s'",
        directory.text().c_str());
    let entries = Path::read_directory(directory);
    if (!entries.has_value()) {
      LOG(verbosity::Debug, "directory '%s' is unreadable, skipping",
          directory.text().c_str());
      continue;
    }
    for (const String &entry : *entries) {
      let const stripped = strip_man1_suffix(entry.view());
      if (!stripped.has_value() || stripped->is_empty()) continue;
      if (MAN_PAGE_FILE_PATHS.find(*stripped) != nullptr) continue;
      let file_path = directory.clone();
      file_path.push_component(entry.view());
      MAN_PAGE_FILE_PATHS.set(*stripped, String{file_path.text().view()});
    }
  }
  /* Candidate order does not matter, complete() sorts, so the second pass
     walks the path map itself rather than a third copy of the names. */
  MAN_PAGE_FILE_PATHS.for_each([&](StringView name, const String &) {
    let const dash = name.find_character('-');
    if (!dash.has_value() || *dash == 0) return;
    let const head = name.substring_of_length(0, *dash);
    let const tail = name.substring(*dash + 1);
    if (tail.is_empty() || (tail[0] >= '0' && tail[0] <= '9')) return;
    if (MAN_PAGE_FILE_PATHS.find(head) == nullptr) return;
    MAN_SUBCOMMAND_INDEX.get_or_create(head, ArrayList<String>{})
        .push(String{tail});
  });
  LOG(verbosity::Info, "indexed %zu section-1 pages",
      MAN_PAGE_FILE_PATHS.count());
}

/* The synopsis region of a man page source, located by its .SH heading or the
   mdoc .Sh form, with the roff font escapes stripped, the escaped dashes
   rewritten, and whitespace runs folded, so the rendered space form is
   searchable as plain bytes. Empty when the page has no synopsis section. */
static fn cleaned_synopsis_of_page(StringView source) throws -> String
{
  let synopsis = String{};
  let is_inside_synopsis = false;
  usize line_start = 0;
  for (usize i = 0; i <= source.length; i++) {
    if (i != source.length && source[i] != '\n') continue;
    let const line = source.substring_of_length(line_start, i - line_start);
    line_start = i + 1;
    if (line.starts_with(".SH") || line.starts_with(".Sh")) {
      let is_synopsis_heading = false;
      for (usize j = 0; j + 8 <= line.length && !is_synopsis_heading; j++)
        is_synopsis_heading = line.substring(j).starts_with("SYNOPSIS");
      if (is_inside_synopsis && !is_synopsis_heading) break;
      is_inside_synopsis = is_synopsis_heading;
      continue;
    }
    if (!is_inside_synopsis) continue;
    for (usize j = 0; j < line.length; j++) {
      let const byte = line[j];
      if (byte == '\\' && j + 1 < line.length) {
        let const escaped = line[j + 1];
        if (escaped == 'f') {
          j += 2;
        } else if (escaped == '-') {
          synopsis.push('-');
          j++;
        } else if (escaped == '&') {
          j++;
        } else {
          synopsis.push(escaped);
          j++;
        }
        continue;
      }
      /* A CR at a CRLF line end folds like whitespace, so a page with DOS
         line endings does not leave a stray byte between the words the space
         form needs to match. */
      if (byte == ' ' || byte == '\t' || byte == '\r') {
        if (!synopsis.is_empty() &&
            synopsis.view()[synopsis.length() - 1] != ' ')
          synopsis.push(' ');
        continue;
      }
      synopsis.push(byte);
    }
    synopsis.push(' ');
  }
  return synopsis;
}

/* Whether the command-subcommand page documents the space-separated form in
   its synopsis, so a real subcommand page such as git-commit.1 that opens its
   synopsis with `git commit` survives, while a standalone dashed tool such as
   ssh-keygen.1 that only writes its literal dashed name does not. A page that
   cannot be read or is compressed keeps its candidate on the head-page rule
   alone. The verdict is cached per page name, so a page is read at most once
   per launch, and the caller validates only the candidates the token matches.
   may_read is false on the ghost path, which then trusts a cached verdict and
   skips an unread candidate rather than scanning a page on a keystroke. */
static fn man_subcommand_page_is_valid(StringView command,
                                       StringView subcommand,
                                       bool may_read) throws -> bool
{
  let page_name = String{command};
  page_name.push('-');
  page_name.append(subcommand);
  if (let const cached = MAN_SUBCOMMAND_PAGE_VALID.find(page_name.view()))
    return *cached;
  if (!may_read) return false;

  let const file_path = MAN_PAGE_FILE_PATHS.find(page_name.view());
  if (file_path == nullptr) {
    MAN_SUBCOMMAND_PAGE_VALID.set(page_name.view(), false);
    return false;
  }

  /* A compressed page cannot be scanned without a decompressor, so the
     candidate stays on the head-page rule alone, with no read at all. */
  let const path_view = file_path->view();
  for (const StringView tail : {StringView{".gz"}, StringView{".xz"},
                                StringView{".zst"}, StringView{".bz2"}})
    if (path_view.length > tail.length &&
        path_view.substring(path_view.length - tail.length) == tail)
    {
      MAN_SUBCOMMAND_PAGE_VALID.set(page_name.view(), true);
      return true;
    }

  let source = utils::read_entire_file(file_path->view());
  if (!source.has_value()) {
    MAN_SUBCOMMAND_PAGE_VALID.set(page_name.view(), true);
    return true;
  }
  /* A page that is one .so redirect reads its target once, relative to the man
     root above the section directory. */
  if (source->view().starts_with(".so ")) {
    let const rest = source->view().substring(4);
    usize target_end = 0;
    while (target_end < rest.length && rest[target_end] != '\n' &&
           rest[target_end] != ' ')
      target_end++;
    let target = Path{file_path->view()}.parent().parent();
    target.push_component(rest.substring_of_length(0, target_end));
    source = utils::read_entire_file(target.text().view());
    if (!source.has_value()) {
      MAN_SUBCOMMAND_PAGE_VALID.set(page_name.view(), true);
      return true;
    }
  }

  let const synopsis = cleaned_synopsis_of_page(source->view());
  let needle = String{command};
  needle.push(' ');
  needle.append(subcommand);
  let const valid = synopsis.find_substring(needle.view()).has_value();
  MAN_SUBCOMMAND_PAGE_VALID.set(page_name.view(), valid);
  return valid;
}

/* Whether the token at token_start is the line's first argument, the word
   right after the command with only blanks between, the slot a subcommand
   completes at. */
static fn is_first_argument_token(StringView line, usize token_start) wontthrow
    -> bool
{
  let const command = command_word_of(line);
  if (command.is_empty()) return false;
  let const command_end =
      static_cast<usize>(command.data - line.data) + command.length;
  if (token_start <= command_end) return false;
  for (usize i = command_end; i < token_start; i++)
    if (line[i] != ' ' && line[i] != '\t') return false;
  return true;
}

/* The line's settled second word past the command, the subcommand slot, None
   when the line has no completed second word or it opens with a dash. */
static fn second_word_of(StringView line) wontthrow -> Maybe<StringView>
{
  let const command = command_word_of(line);
  if (command.is_empty()) return None;
  let i = static_cast<usize>(command.data - line.data) + command.length;
  while (i < line.length && (line[i] == ' ' || line[i] == '\t'))
    i++;
  let const start = i;
  while (i < line.length && line[i] != ' ' && line[i] != '\t')
    i++;
  /* A word the cursor still sits in has no separator after it, so it is the
     token under completion rather than a settled subcommand. */
  if (i >= line.length) return None;
  let const word = line.substring_of_length(start, i - start);
  if (word.is_empty() || word[0] == '-') return None;
  return word;
}

/* Completes the first argument of a command from the subcommand index, so git
   com offers commit the way the git-commit page promises. The index builds
   once per launch on an explicit tab. The ghost path reads only an already
   built and validated entry, so a keystroke never scans a directory or reads
   a page. None means the position or the command has no subcommand story and
   the caller falls through to the option, spec, and filesystem stages. */
static fn complete_from_man_subcommands(StringView line, StringView token,
                                        usize token_start, bool for_listing,
                                        EvalContext &context) throws
    -> Maybe<ArrayList<String>>
{
  if (!token.is_empty() && token[0] == '-') return None;
  if (token.find_character('/').has_value()) return None;
  if (!for_listing && token.is_empty()) return None;
  if (!is_first_argument_token(line, token_start)) return None;
  let const surface_command = command_word_of(line);
  if (surface_command.is_empty() ||
      surface_command.find_character('/').has_value())
    return None;
  /* The subcommands are the resolved target's, so g for a g='git' alias
     lists git's subcommands. */
  let const resolved = resolve_completion_command(surface_command, context);
  let const command = resolved.view();

  if (!MAN_SUBCOMMAND_INDEX_IS_BUILT) {
    if (!for_listing) return None;
    build_man_subcommand_index();
  }

  let const subcommands = MAN_SUBCOMMAND_INDEX.find(command);
  if (subcommands == nullptr || subcommands->is_empty()) return None;

  /* Only the candidates the token matches are validated, so a token that
     matches no subcommand, such as a typo, reads no page at all and the prompt
     does not stall on a command with a hundred subcommand pages. */
  let matches = ArrayList<String>{};
  for (const String &subcommand : *subcommands)
    if (subcommand.view().starts_with(token) &&
        man_subcommand_page_is_valid(command, subcommand.view(), for_listing))
      matches.push(String{subcommand.view()});
  LOG(verbosity::Debug, "%zu subcommands of '%.*s' match token '%.*s'",
      matches.count(), static_cast<int>(command.length), command.data,
      static_cast<int>(token.length), token.data);
  if (matches.is_empty()) return None;
  return matches;
}

/* Pulls each dash-word out of an option line's tag part, such as -a and --all
   from `-a, --all`, dropping a trailing =VALUE so the bare flag completes. The
   manpage tag line and the --help option column both split this way, so both
   parsers call it. */
static fn extract_dash_flags(StringView option_part) throws -> ArrayList<String>
{
  let flags = ArrayList<String>{};
  usize k = 0;
  while (k < option_part.length) {
    while (k < option_part.length &&
           (option_part[k] == ' ' || option_part[k] == ',' ||
            option_part[k] == '\t'))
      k++;
    let const token_start = k;
    while (k < option_part.length && option_part[k] != ' ' &&
           option_part[k] != ',' && option_part[k] != '\t')
      k++;
    let flag = option_part.substring_of_length(token_start, k - token_start);
    if (let const equals = flag.find_character('='); equals.has_value())
      flag = flag.substring_of_length(0, *equals);
    if (flag.length >= 2 && flag[0] == '-') flags.push(String{flag});
  }
  return flags;
}

/* The options a command's manpage documents, each paired with the description
   in its .TP block. The flag set is the same word scan as before, every -x and
   --long at a word boundary, so the candidate list is unchanged, while a
   line-oriented pass over the page records the description that sits inline
   after the tag or on the indented line below it. man's overstrike formatting,
   a byte backspace byte for bold and an underscore backspace char for an
   underline, is stripped first. */
static fn parse_manpage_option_entries(StringView text) throws
    -> ArrayList<help_entry>
{
  let clean = String{};
  clean.reserve(text.length);
  for (usize i = 0; i < text.length; i++) {
    if (text[i] == '\b') {
      if (!clean.is_empty()) clean.pop_back();
      continue;
    }
    clean += text[i];
  }
  let const view = clean.view();

  /* The description for each flag, read from the .TP option blocks. man wraps a
     long description across several lines, inline after a long option or
     indented below a short one, so the lines are joined into the whole
     description rather than keeping only the first. */
  let descriptions = StringMap<String>{heap_allocator()};
  let pending_flags = ArrayList<String>{};
  usize pending_indent = 0;
  let pending_description = String{};

  let finalize_pending = [&]() throws -> void {
    if (pending_flags.is_empty()) return;
    let const desc = trim_blanks(pending_description.view());
    for (const String &flag : pending_flags)
      if (!desc.is_empty() && descriptions.find(flag.view()) == nullptr)
        descriptions.set(flag.view(), String{desc});
    pending_flags.clear();
    pending_description = String{};
  };

  usize i = 0;
  while (i < view.length) {
    let line_end = i;
    while (line_end < view.length && view[line_end] != '\n')
      line_end++;
    let const raw = view.substring_of_length(i, line_end - i);
    i = line_end + 1;

    let const indent = skip_blanks(raw, 0);
    /* A blank line ends the current option's description block. */
    if (indent >= raw.length) {
      finalize_pending();
      continue;
    }

    /* A line with no leading dash, at the option's indent or deeper, continues
       the wrapped description man broke across lines, whether man wrapped it
       inline after a long option or indented below a short one. */
    if (!pending_flags.is_empty() && raw[indent] != '-' &&
        indent >= pending_indent)
    {
      let const piece =
          trim_blanks(raw.substring_of_length(indent, raw.length - indent));
      if (!piece.is_empty()) {
        if (!pending_description.is_empty()) pending_description += ' ';
        pending_description.append(piece);
      }
      continue;
    }

    /* Any other line ends the pending block, and a dash line opens a new one.
     */
    finalize_pending();
    if (raw[indent] != '-') continue;

    let gap = raw.length;
    for (usize j = indent; j + 1 < raw.length; j++)
      if (raw[j] == ' ' && raw[j + 1] == ' ') {
        gap = j;
        break;
      }
    let const option_part = raw.substring_of_length(indent, gap - indent);
    pending_flags = extract_dash_flags(option_part);
    pending_indent = indent;
    pending_description = String{};
    if (gap < raw.length)
      pending_description.append(
          trim_blanks(raw.substring_of_length(gap, raw.length - gap)));
  }
  finalize_pending();

  /* The authoritative flag list is the word scan, so the candidate set matches
     the prior behavior, with the description attached where the block pass
     found one. */
  let entries = ArrayList<help_entry>{};
  let seen = HashSet{heap_allocator()};
  for (usize j = 0; j < view.length; j++) {
    let const at_word_start = j == 0 || view[j - 1] == ' ' ||
                              view[j - 1] == '\t' || view[j - 1] == '\n' ||
                              view[j - 1] == '(' || view[j - 1] == '[';
    if (view[j] != '-' || !at_word_start) continue;
    let end = j;
    while (end < view.length &&
           (view[end] == '-' || lexer::is_variable_name(view[end])))
      end++;
    let const flag = view.substring_of_length(j, end - j);
    let has_letter = false;
    for (usize k = 0; k < flag.length; k++)
      if (flag[k] != '-') {
        has_letter = !(flag[k] >= '0' && flag[k] <= '9');
        if (has_letter) break;
      }
    if (flag.length >= 2 && has_letter && !seen.contains(flag)) {
      seen.add(flag);
      let const description = descriptions.find(flag);
      entries.push(help_entry{String{flag}, description != nullptr
                                                ? String{description->view()}
                                                : String{}});
    }
    j = end;
  }
  entries.shrink_to_fit();
  return entries;
}

/* The commands shit is allowed to fork for their --help text, each mapped to
   the help argument that prints the full option and subcommand list. This is
   the allowlist half of the gate, so a command must appear here and resolve
   into a trusted directory before its --help runs. The value is the help
   argument, almost always --help, ffmpeg and its siblings the exception since
   they print the complete set only for --help full. A command whose argument is
   not plain --help reads its options from --help rather than a manpage, so its
   single-dash long options come through even when a manpage also exists.

   The list names common tools whose --help lists flags and subcommands, the
   ones a zero-config shell should complete without a hand-written script,
   leaning toward tools that lack a good manpage such as cargo. A name longer
   than sixteen bytes cannot key the packed map, so it is left out. */
static constexpr StaticStringMap<const char *>::entry HELP_ALLOWLIST_ENTRIES[] =
    {
        {SSK("ffmpeg"),       "--help full"},
        {SSK("ffprobe"),      "--help full"},
        {SSK("ffplay"),       "--help full"},
        {SSK("cargo"),        "--help"     },
        {SSK("tailscale"),    "--help"     },
        {SSK("rustup"),       "--help"     },
        {SSK("rustc"),        "--help"     },
        {SSK("rustfmt"),      "--help"     },
        {SSK("go"),           "--help"     },
        {SSK("npm"),          "--help"     },
        {SSK("pnpm"),         "--help"     },
        {SSK("yarn"),         "--help"     },
        {SSK("deno"),         "--help"     },
        {SSK("bun"),          "--help"     },
        {SSK("node"),         "--help"     },
        {SSK("pip"),          "--help"     },
        {SSK("pip3"),         "--help"     },
        {SSK("docker"),       "--help"     },
        {SSK("podman"),       "--help"     },
        {SSK("kubectl"),      "--help"     },
        {SSK("helm"),         "--help"     },
        {SSK("gh"),           "--help"     },
        {SSK("glab"),         "--help"     },
        {SSK("terraform"),    "--help"     },
        {SSK("cmake"),        "--help"     },
        {SSK("ninja"),        "--help"     },
        {SSK("meson"),        "--help"     },
        {SSK("jq"),           "--help"     },
        {SSK("yq"),           "--help"     },
        {SSK("rg"),           "--help"     },
        {SSK("fd"),           "--help"     },
        {SSK("bat"),          "--help"     },
        {SSK("fzf"),          "--help"     },
        {SSK("zig"),          "--help"     },
        {SSK("poetry"),       "--help"     },
        {SSK("pipx"),         "--help"     },
        {SSK("uv"),           "--help"     },
        {SSK("just"),         "--help"     },
        {SSK("hugo"),         "--help"     },
        {SSK("pandoc"),       "--help"     },
        {SSK("delta"),        "--help"     },
        {SSK("dust"),         "--help"     },
        {SSK("starship"),     "--help"     },
        {SSK("gofmt"),        "--help"     },
        {SSK("magick"),       "--help"     },
        {SSK("convert"),      "--help"     },
        /* Compilers and binary tools. */
        {SSK("clang"),        "--help"     },
        {SSK("clang++"),      "--help"     },
        {SSK("gcc"),          "--help"     },
        {SSK("g++"),          "--help"     },
        {SSK("cc"),           "--help"     },
        {SSK("c++"),          "--help"     },
        {SSK("clang-format"), "--help"     },
        {SSK("clang-tidy"),   "--help"     },
        {SSK("gdb"),          "--help"     },
        {SSK("lldb"),         "--help"     },
        {SSK("objdump"),      "--help"     },
        {SSK("readelf"),      "--help"     },
        {SSK("nm"),           "--help"     },
        {SSK("strip"),        "--help"     },
        {SSK("ar"),           "--help"     },
        {SSK("ld"),           "--help"     },
        {SSK("lld"),          "--help"     },
        {SSK("valgrind"),     "--help"     },
        {SSK("cppcheck"),     "--help"     },
        {SSK("ccache"),       "--help"     },
        /* Language runtimes and their toolchains. */
        {SSK("python"),       "--help"     },
        {SSK("python3"),      "--help"     },
        {SSK("ruby"),         "--help"     },
        {SSK("perl"),         "--help"     },
        {SSK("lua"),          "--help"     },
        {SSK("php"),          "--help"     },
        {SSK("julia"),        "--help"     },
        {SSK("java"),         "--help"     },
        {SSK("javac"),        "--help"     },
        {SSK("kotlin"),       "--help"     },
        {SSK("kotlinc"),      "--help"     },
        {SSK("scala"),        "--help"     },
        {SSK("dotnet"),       "--help"     },
        {SSK("dart"),         "--help"     },
        {SSK("swift"),        "--help"     },
        {SSK("swiftc"),       "--help"     },
        {SSK("elixir"),       "--help"     },
        {SSK("mix"),          "--help"     },
        {SSK("ocaml"),        "--help"     },
        {SSK("crystal"),      "--help"     },
        {SSK("nim"),          "--help"     },
        {SSK("cabal"),        "--help"     },
        {SSK("stack"),        "--help"     },
        {SSK("opam"),         "--help"     },
        {SSK("ghc"),          "--help"     },
        {SSK("tsc"),          "--help"     },
        {SSK("esbuild"),      "--help"     },
        {SSK("prettier"),     "--help"     },
        {SSK("eslint"),       "--help"     },
        {SSK("biome"),        "--help"     },
        {SSK("vite"),         "--help"     },
        /* Package managers. */
        {SSK("apt"),          "--help"     },
        {SSK("apt-get"),      "--help"     },
        {SSK("dnf"),          "--help"     },
        {SSK("pacman"),       "--help"     },
        {SSK("zypper"),       "--help"     },
        {SSK("apk"),          "--help"     },
        {SSK("brew"),         "--help"     },
        {SSK("flatpak"),      "--help"     },
        {SSK("snap"),         "--help"     },
        {SSK("nix"),          "--help"     },
        {SSK("conda"),        "--help"     },
        {SSK("mamba"),        "--help"     },
        {SSK("gem"),          "--help"     },
        {SSK("composer"),     "--help"     },
        /* Version control. */
        {SSK("hg"),           "--help"     },
        {SSK("svn"),          "--help"     },
        {SSK("jj"),           "--help"     },
        {SSK("fossil"),       "--help"     },
        /* Modern command-line tools. */
        {SSK("eza"),          "--help"     },
        {SSK("lsd"),          "--help"     },
        {SSK("procs"),        "--help"     },
        {SSK("sd"),           "--help"     },
        {SSK("hyperfine"),    "--help"     },
        {SSK("tokei"),        "--help"     },
        {SSK("watchexec"),    "--help"     },
        {SSK("entr"),         "--help"     },
        {SSK("direnv"),       "--help"     },
        {SSK("zoxide"),       "--help"     },
        {SSK("atuin"),        "--help"     },
        {SSK("broot"),        "--help"     },
        {SSK("btm"),          "--help"     },
        {SSK("gitui"),        "--help"     },
        {SSK("dive"),         "--help"     },
        {SSK("k9s"),          "--help"     },
        {SSK("kubectx"),      "--help"     },
        {SSK("kubens"),       "--help"     },
        {SSK("kustomize"),    "--help"     },
        {SSK("skaffold"),     "--help"     },
        /* Cloud and infrastructure. */
        {SSK("doctl"),        "--help"     },
        {SSK("flyctl"),       "--help"     },
        {SSK("pulumi"),       "--help"     },
        {SSK("packer"),       "--help"     },
        {SSK("vault"),        "--help"     },
        {SSK("consul"),       "--help"     },
        {SSK("nomad"),        "--help"     },
        {SSK("vercel"),       "--help"     },
        /* Network and transfer. */
        {SSK("curl"),         "--help"     },
        {SSK("wget"),         "--help"     },
        {SSK("httpie"),       "--help"     },
        {SSK("xh"),           "--help"     },
        {SSK("aria2c"),       "--help"     },
        {SSK("rsync"),        "--help"     },
        {SSK("rclone"),       "--help"     },
        /* Text and data. */
        {SSK("mlr"),          "--help"     },
        {SSK("dasel"),        "--help"     },
        {SSK("gron"),         "--help"     },
        {SSK("fx"),           "--help"     },
        {SSK("xsv"),          "--help"     },
        /* System. */
        {SSK("systemctl"),    "--help"     },
        {SSK("journalctl"),   "--help"     },
        {SSK("nmcli"),        "--help"     },
        {SSK("buildah"),      "--help"     },
        {SSK("skopeo"),       "--help"     },
        {SSK("dedoc"),        "--help"     },
        {SSK("typst"),        "--help"     },
};
static constexpr StaticStringMap<const char *> HELP_ALLOWLIST{
    HELP_ALLOWLIST_ENTRIES,
    sizeof(HELP_ALLOWLIST_ENTRIES) / sizeof(HELP_ALLOWLIST_ENTRIES[0])};

/* Whether the command reads its options from --help in preference to a manpage,
   so the manpage stage skips it. Only a command whose help argument is not the
   plain --help qualifies, the ffmpeg family, whose manpage carries the options
   in a form the flag scanner does not read. */
static fn command_prefers_help_over_manpage(StringView command) throws -> bool
{
  let argument = HELP_ALLOWLIST.find(command);
  return argument.has_value() && StringView{*argument} != StringView{"--help"};
}

/* Defined below, declared here so the man fork can reuse the same trusted
   directory gate the --help fork uses. */
static fn command_directory_is_trusted(StringView absolute_path) throws -> bool;

/* The option flags a manpage documents, parsed once and cached under the page
   name. The man invocation is the general path that works for any command on
   the host, so the completer is not limited to a hardcoded set of tools. */
static fn manpage_options_for(StringView page_name, EvalContext &context) throws
    -> const ArrayList<help_entry> &
{
  if (let const cached = MANPAGE_OPTION_CACHE.find(page_name)) return *cached;
  let parsed = ArrayList<help_entry>{};
  /* man forks only when it resolves into a trusted directory such as /usr/bin,
     so an alias, a function, or a man planted in a world-writable directory is
     never run. The resolved absolute path runs in place of the bare name, so
     the command substitution cannot reresolve it through PATH, while the shell
     still applies the 2>/dev/null redirection. A man in an untrusted directory
     or no man at all caches the empty list, so the page never forks twice. */
  let const man_paths = utils::search_program_path("man");
  if (man_paths.is_empty() ||
      !command_directory_is_trusted(man_paths[0].text().view()))
  {
    LOG(verbosity::Debug,
        "skipping the man fork for '%.*s' because man is absent or untrusted",
        static_cast<int>(page_name.length), page_name.data);
    MANPAGE_OPTION_CACHE.set(page_name, steal(parsed));
    return *MANPAGE_OPTION_CACHE.find(page_name);
  }
  try {
    let const page = context.capture_command_substitution(
        String{man_paths[0].text().view()} + " " + String{page_name} +
        " 2>/dev/null");
    parsed = parse_manpage_option_entries(page.view());
  } catch (...) {
    LOG(verbosity::Debug, "swallowed a man invocation failure for '%.*s'",
        static_cast<int>(page_name.length), page_name.data);
  }
  MANPAGE_OPTION_CACHE.set(page_name, steal(parsed));
  return *MANPAGE_OPTION_CACHE.find(page_name);
}

/* Completes an option token from the command's manpage, the general source
   that needs no per-command table. Runs only on an explicit tab and only for a
   token that opens with a dash, so the ghost never forks man and a plain
   argument still completes as a filename. None means the man path did not
   apply, so the caller falls through to the spec and the filesystem. */
static fn complete_from_manpage(StringView line, StringView token,
                                bool for_listing, EvalContext &context,
                                StringMap<String> &descriptions) throws
    -> Maybe<ArrayList<String>>
{
  if (!for_listing) return None;
  if (token.is_empty() || token[0] != '-') return None;
  let const surface_command = command_word_of(line);
  if (surface_command.is_empty() ||
      surface_command.find_character('/').has_value())
    return None;

  /* The manpage is the resolved target's, so an aliased or symlinked command
     reads the options of what it really runs. */
  let const resolved = resolve_completion_command(surface_command, context);
  let const command = resolved.view();

  /* A command that prefers --help reads its options from there rather than the
     manpage, so it skips this stage and the help stage below picks it up. */
  if (command_prefers_help_over_manpage(command)) return None;

  /* git commit -<tab> reads the git-commit page when the index knows it, the
     general command-subcommand form, so the options come from the subcommand
     page rather than the umbrella one. The explicit-tab gate above already
     holds here, so building the index is as lazy as the man fork itself. */
  let page_name = manpage_name_for(command);
  if (let const subcommand_word = second_word_of(line);
      subcommand_word.has_value())
  {
    if (!MAN_SUBCOMMAND_INDEX_IS_BUILT) build_man_subcommand_index();
    let combined = String{command};
    combined.push('-');
    combined.append(*subcommand_word);
    if (MAN_PAGE_FILE_PATHS.find(combined.view()) != nullptr)
      page_name = steal(combined);
  }

  let const &options = manpage_options_for(page_name.view(), context);
  if (options.is_empty()) return None;

  let matches = matches_from_help_entries(options, token, descriptions);
  if (matches.is_empty()) return None;
  return matches;
}

/* The option flags a command's --help text lists, and the subcommands it
   lists, each cached under the resolved command name. One fork parses both, so
   the raw text frees right after parsing rather than living for the session.
   The forked set records a command whose --help ran, so a command that yields
   no options and no subcommands still never forks twice. */
static StringMap<ArrayList<help_entry>> HELP_OPTION_CACHE{heap_allocator()};
static StringMap<ArrayList<help_entry>> HELP_SUBCOMMAND_CACHE{heap_allocator()};
static StringMap<bool> HELP_PARSED{heap_allocator()};

/* Whether the directory the resolved binary sits in is safe to fork for its
   --help text. The check is permission-based rather than a fixed list, so a
   user tool directory such as ~/.cargo/bin or ~/.local/bin is trusted while a
   world-writable directory such as /tmp or a path an attacker prepended is not.
   shit derives completions with no config, so the directory set cannot be a
   hardcoded list of system bins. */
static fn command_directory_is_trusted(StringView absolute_path) throws -> bool
{
  let last_slash = absolute_path.length;
  for (usize i = 0; i < absolute_path.length; i++)
    if (absolute_path[i] == '/') last_slash = i;
  if (last_slash == absolute_path.length) return false;
  let const directory = last_slash == 0
                            ? StringView{"/"}
                            : absolute_path.substring_of_length(0, last_slash);
  return os::directory_is_trusted_for_exec(Path{directory});
}

/* The wall-clock budget a single --help fork is allowed. A command whose --help
   runs longer is killed and caches the empty string, so the prompt never
   freezes on it and it is never forked again this session. */
static constexpr u64 HELP_FORK_TIMEOUT_NANOS = 1'000'000'000;

/* A command's raw --help text, captured once. The fork passes two gates, the
   command is on the allowlist and resolves into a trusted directory, so a
   program shit does not recognize and a program in an untrusted place are both
   left unforked. The resolved absolute path runs as the only argv entry before
   the help argument, not through a shell, so an alias or a function never
   shadows the trusted binary, and stdin is the null device so a binary that
   would page or prompt reads end-of-file and exits. The capture is bounded by
   the timeout, and a command that fails any of these caches the empty string,
   so it never forks twice. */
static fn help_text_for(StringView command) throws -> String
{
  let text = String{};
  /* The allowlist entry carries the help argument, so ffmpeg forks --help full
     rather than the summary-only --help. A command not on the list never
     forks. */
  let help_argument = HELP_ALLOWLIST.find(command);
  let const paths = utils::search_program_path(command);
  if (help_argument.has_value() && !paths.is_empty() &&
      command_directory_is_trusted(paths[0].text().view()))
  {
    /* argv is the absolute path then the help argument split on spaces, so
       ffmpeg runs as the three words path, --help, full. */
    let argv = ArrayList<String>{};
    argv.push(String{paths[0].text().view()});
    let const argument_view = StringView{*help_argument};
    usize i = 0;
    while (i < argument_view.length) {
      while (i < argument_view.length && argument_view[i] == ' ')
        i++;
      let const start = i;
      while (i < argument_view.length && argument_view[i] != ' ')
        i++;
      if (i > start)
        argv.push(String{argument_view.substring_of_length(start, i - start)});
    }
    LOG(verbosity::Debug, "forking '%.*s' for its --help text",
        static_cast<int>(command.length), command.data);
    if (Maybe<String> output =
            os::capture_program_output(argv, HELP_FORK_TIMEOUT_NANOS);
        output.has_value())
      text = steal(*output);
  }
  return text;
}

/* The dash-options a --help text lists, each paired with the description in the
   column beside it. A line that opens with an option is split at the first run
   of two or more spaces, the option part before it and the description after.
   Every dash-word in the option part maps to the one description, so cargo's
   -v, --verbose pair and ffmpeg's -pix_fmt with an argument placeholder both
   read their text. A trailing =VALUE on a flag is dropped so the bare flag
   completes. */
static fn parse_help_option_entries(StringView text) throws
    -> ArrayList<help_entry>
{
  let entries = ArrayList<help_entry>{};
  let seen = HashSet{heap_allocator()};
  usize i = 0;
  while (i < text.length) {
    let line_end = i;
    while (line_end < text.length && text[line_end] != '\n')
      line_end++;
    let const raw = text.substring_of_length(i, line_end - i);
    i = line_end + 1;

    let const start = skip_blanks(raw, 0);
    if (start >= raw.length || raw[start] != '-') continue;

    /* The first run of two or more spaces ends the option part and opens the
       description column. */
    let gap = raw.length;
    for (usize j = start; j + 1 < raw.length; j++)
      if (raw[j] == ' ' && raw[j + 1] == ' ') {
        gap = j;
        break;
      }
    let const option_part = raw.substring_of_length(start, gap - start);

    let description = StringView{};
    if (gap < raw.length)
      description = trim_blanks(raw.substring_of_length(gap, raw.length - gap));

    for (const String &flag : extract_dash_flags(option_part))
      if (!seen.contains(flag.view())) {
        seen.add(flag.view());
        entries.push(help_entry{String{flag.view()}, String{description}});
      }
  }
  return entries;
}

static fn parse_help_subcommands(StringView text) throws
    -> ArrayList<help_entry>;

/* Forks the command's --help once, parses both the options and the
   subcommands out of the one capture, and frees the raw text. The forked set
   gates the fork, so a second tab on the same command reads the parsed caches
   without forking and without holding the whole --help text for the session. */
static fn ensure_help_parsed(StringView command) throws -> void
{
  if (HELP_PARSED.find(command) != nullptr) return;
  let const text = help_text_for(command);
  HELP_OPTION_CACHE.set(command, parse_help_option_entries(text.view()));
  HELP_SUBCOMMAND_CACHE.set(command, parse_help_subcommands(text.view()));
  HELP_PARSED.set(command, true);
}

/* The options a command's --help text lists, parsed once and cached. */
static fn help_options_for(StringView command) throws
    -> const ArrayList<help_entry> &
{
  ensure_help_parsed(command);
  return *HELP_OPTION_CACHE.find(command);
}

/* Whether a name reads as a subcommand rather than a fragment of a description,
   so it is non-empty, opens with a letter or a digit, and carries only the
   characters a subcommand uses. */
static fn is_plausible_subcommand_name(StringView name) wontthrow -> bool
{
  if (name.is_empty()) return false;
  let const first = name[0];
  let const starts_word = (first >= 'a' && first <= 'z') ||
                          (first >= 'A' && first <= 'Z') ||
                          (first >= '0' && first <= '9');
  if (!starts_word) return false;
  for (usize i = 0; i < name.length; i++) {
    let const c = name[i];
    let const ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok) return false;
  }
  return true;
}

/* Whether a header line opens a subcommand section, so any line that reads
   "Commands:", "Available Commands:", "Subcommands:", and the like, matched
   case-insensitively on the "commands:" or "subcommands:" tail. */
static fn line_opens_subcommand_section(StringView trimmed) wontthrow -> bool
{
  if (trimmed.is_empty() || trimmed[trimmed.length - 1] != ':') return false;
  let const ends_with_ignoring_case = [&](StringView suffix) {
    if (trimmed.length < suffix.length) return false;
    let const offset = trimmed.length - suffix.length;
    for (usize i = 0; i < suffix.length; i++) {
      let a = trimmed[offset + i];
      let b = suffix[i];
      if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
      if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
      if (a != b) return false;
    }
    return true;
  };
  return ends_with_ignoring_case(StringView{"commands:"}) ||
         ends_with_ignoring_case(StringView{"subcommands:"});
}

/* The subcommands a --help text lists under a commands section. cargo and other
   tools with subcommands but no manpage list them under a "Commands:" header as
   indented "name<spaces>description" or "name, alias<spaces>description" lines.
   The scan reads the first token of each indented line under such a header,
   drops options and the ... continuation marker, and stops at a blank line or
   a line that returns to the left margin. */
static fn parse_help_subcommands(StringView text) throws
    -> ArrayList<help_entry>
{
  let subcommands = ArrayList<help_entry>{};
  let seen = HashSet{heap_allocator()};
  let in_section = false;
  usize i = 0;
  while (i < text.length) {
    let line_end = i;
    while (line_end < text.length && text[line_end] != '\n')
      line_end++;
    let const raw = text.substring_of_length(i, line_end - i);
    i = line_end + 1;

    let const trim_start = skip_blanks(raw, 0);
    let const trimmed = trim_blanks(raw);

    if (line_opens_subcommand_section(trimmed)) {
      in_section = true;
      continue;
    }
    if (!in_section) continue;
    if (trimmed.is_empty()) {
      in_section = false;
      continue;
    }
    /* A line that returns to the left margin ends the section, and may open a
       new one of its own. */
    if (trim_start == 0) {
      in_section = line_opens_subcommand_section(trimmed);
      continue;
    }

    usize name_end = 0;
    while (name_end < trimmed.length && trimmed[name_end] != ' ' &&
           trimmed[name_end] != '\t' && trimmed[name_end] != ',')
      name_end++;
    let const name = trimmed.substring_of_length(0, name_end);
    if (!is_plausible_subcommand_name(name)) continue;
    if (!seen.contains(name)) {
      seen.add(name);
      /* The description is the text after the run of two or more spaces that
         ends the name-and-alias column, trimmed. */
      let description = StringView{};
      let gap = trimmed.length;
      for (usize j = name_end; j + 1 < trimmed.length; j++)
        if (trimmed[j] == ' ' && trimmed[j + 1] == ' ') {
          gap = j;
          break;
        }
      if (gap < trimmed.length)
        description =
            trim_blanks(trimmed.substring_of_length(gap, trimmed.length - gap));
      subcommands.push(help_entry{String{name}, String{description}});
    }
  }
  return subcommands;
}

/* The subcommands a command's --help text lists, parsed once and cached. */
static fn help_subcommands_for(StringView command) throws
    -> const ArrayList<help_entry> &
{
  ensure_help_parsed(command);
  return *HELP_SUBCOMMAND_CACHE.find(command);
}

/* Completes an option token from the command's --help text, the fallback after
   the manpage stage finds no page. The same explicit-tab and dash-token gates
   the manpage stage uses hold here, so the ghost never forks a command and a
   plain argument still completes as a filename. */
static fn complete_from_help(StringView line, StringView token,
                             bool for_listing, EvalContext &context,
                             StringMap<String> &descriptions) throws
    -> Maybe<ArrayList<String>>
{
  if (!for_listing) return None;
  if (token.is_empty() || token[0] != '-') return None;
  let const surface_command = command_word_of(line);
  if (surface_command.is_empty() ||
      surface_command.find_character('/').has_value())
    return None;

  /* The alias-only name keeps a multiplexer link such as cargo to rustup at the
     surface name, so the --help fork dispatches on the typed argv[0]. */
  let const resolved = resolve_completion_alias(surface_command, context);
  let const &options = help_options_for(resolved.view());
  if (options.is_empty()) return None;

  let matches = matches_from_help_entries(options, token, descriptions);
  if (matches.is_empty()) return None;
  return matches;
}

/* Completes a subcommand token from the command's --help text, for a tool such
   as cargo that lists subcommands but has no manpage to read them from. The
   subcommand-position, explicit-tab, and dash gates match the manpage
   subcommand stage, so this never fires on an option token or for the ghost. */
static fn complete_from_help_subcommands(StringView line, StringView token,
                                         usize token_start, bool for_listing,
                                         EvalContext &context,
                                         StringMap<String> &descriptions) throws
    -> Maybe<ArrayList<String>>
{
  if (!for_listing) return None;
  if (!token.is_empty() && token[0] == '-') return None;
  if (token.find_character('/').has_value()) return None;
  if (!is_first_argument_token(line, token_start)) return None;
  let const surface_command = command_word_of(line);
  if (surface_command.is_empty() ||
      surface_command.find_character('/').has_value())
    return None;

  /* The alias-only name keeps a multiplexer link such as cargo to rustup at the
     surface name, so the --help fork dispatches on the typed argv[0]. */
  let const resolved = resolve_completion_alias(surface_command, context);

  /* A command the man index already lists subcommands for, such as kubectl or
     git, never forks --help to relist them, since the man stage that ran before
     this one is the authoritative source. The fork is reserved for a tool like
     cargo that has subcommands but no man pages. */
  if (MAN_SUBCOMMAND_INDEX_IS_BUILT) {
    let const man_subcommands = MAN_SUBCOMMAND_INDEX.find(resolved.view());
    if (man_subcommands != nullptr && !man_subcommands->is_empty()) return None;
  }

  let const &subcommands = help_subcommands_for(resolved.view());
  if (subcommands.is_empty()) return None;

  let matches = matches_from_help_entries(subcommands, token, descriptions);
  if (matches.is_empty()) return None;
  return matches;
}

/* The settled word right before the token, so set -o NAME completion sees
   the -o. The scan is the same unquoted whitespace reading command_word_of
   applies. */
static fn previous_settled_word(StringView line, usize token_start) wontthrow
    -> StringView
{
  let end = token_start;
  while (end > 0 && (line[end - 1] == ' ' || line[end - 1] == '\t'))
    end--;
  let start = end;
  while (start > 0 && line[start - 1] != ' ' && line[start - 1] != '\t')
    start--;
  return line.substring_of_length(start, end - start);
}

/* One tool's cached target list, keyed by its source file's absolute path
   and refreshed when the file's mtime moves, so a second tab on the same
   Makefile pays no fork. */
struct cached_target_list
{
  i64 mtime;
  ArrayList<String> targets;
};
static StringMap<cached_target_list> BUILD_TARGET_CACHE{heap_allocator()};

/* The value of a -C or -f style option already settled on the line, reading
   both the separated "-C dir" and the attached "-Cdir" spellings the way the
   tools accept them. */
static fn settled_option_value(StringView line, StringView option) throws
    -> Maybe<String>
{
  usize cword = 0;
  let const words = split_completion_words(line, line.length, cword);
  for (usize i = 1; i < words.count(); i++) {
    let const word = words[i].view();
    if (word == option && i + 1 < words.count() && i + 1 != cword)
      return String{words[i + 1].view()};
    if (word.length > option.length && word.starts_with(option))
      return String{word.substring(option.length)};
  }
  return None;
}

/* The targets of a GNU make database dump, the lines between "# Files" and
   "# Finished Make data base" that open a rule. A comment, a dot rule, a
   recipe line, and the rule a "# Not a target" comment disowns are all
   skipped, the same reading fish's make completion applies. */
static fn parse_make_database_targets(StringView database) throws
    -> ArrayList<String>
{
  let targets = ArrayList<String>{};
  let in_files_section = false;
  let skip_next_rule = false;
  usize i = 0;
  while (i < database.length) {
    let end = i;
    while (end < database.length && database[end] != '\n')
      end++;
    let const text = database.substring_of_length(i, end - i);
    i = end + 1;

    if (text.starts_with(StringView{"# Files"})) {
      in_files_section = true;
      continue;
    }
    if (text.starts_with(StringView{"# Finished Make data base"})) break;
    if (!in_files_section) continue;
    if (text.starts_with(StringView{"# Not a target"})) {
      skip_next_rule = true;
      continue;
    }
    if (text.is_empty() || text[0] == '#') continue;
    /* The disowned rule line follows its comment block immediately, so the
       first non-comment line consumes the flag whatever its shape, or a
       .SUFFIXES disown would eat the next real target. */
    if (skip_next_rule) {
      skip_next_rule = false;
      continue;
    }
    if (text[0] == '.' || text[0] == '\t') continue;
    let const colon = text.find_character(':');
    if (!colon.has_value() || *colon == 0) continue;
    targets.push(String{text.substring_of_length(0, *colon)});
  }
  return targets;
}

/* The first line-leading name before a colon out of each line, the shape
   ninja -t targets prints. */
static fn parse_colon_led_names(StringView listing) throws -> ArrayList<String>
{
  let names = ArrayList<String>{};
  usize i = 0;
  while (i < listing.length) {
    let end = i;
    while (end < listing.length && listing[end] != '\n')
      end++;
    let const text = listing.substring_of_length(i, end - i);
    i = end + 1;
    let const colon = text.find_character(':');
    if (!colon.has_value() || *colon == 0) continue;
    let const name = text.substring_of_length(0, *colon);
    if (name.find_character(' ').has_value()) continue;
    names.push(String{name});
  }
  return names;
}

/* The script names of a package.json "scripts" table, collected by a tolerant
   scan that tracks only strings, escapes, and brace nesting, so no JSON
   machinery is linked in. */
static fn parse_package_json_scripts(StringView text) throws
    -> ArrayList<String>
{
  let scripts = ArrayList<String>{};
  let const section = StringView{"\"scripts\""};
  usize at = 0;
  let found = false;
  for (; at + section.length <= text.length; at++)
    if (text.substring_of_length(at, section.length) == section) {
      found = true;
      break;
    }
  if (!found) return scripts;
  let i = at + section.length;
  while (i < text.length && text[i] != '{')
    i++;
  if (i >= text.length) return scripts;
  i++;
  usize depth = 1;
  let expecting_key = true;
  while (i < text.length && depth > 0) {
    let const byte = text[i];
    if (byte == '"') {
      let const start = ++i;
      while (i < text.length && text[i] != '"') {
        if (text[i] == '\\') i++;
        i++;
      }
      if (expecting_key && depth == 1)
        scripts.push(String{text.substring_of_length(start, i - start)});
      expecting_key = false;
      i++;
      continue;
    }
    if (byte == ':') expecting_key = false;
    if (byte == ',') expecting_key = true;
    if (byte == '{') depth++;
    if (byte == '}') depth--;
    i++;
  }
  return scripts;
}

/* The host names an ssh invocation can reach, the Host lines of the user's
   ssh config without the glob patterns, and the first fields of known_hosts
   without the hashed rows. */
static fn collect_ssh_hosts() throws -> ArrayList<String>
{
  let hosts = ArrayList<String>{};
  let const home = os::get_home_directory();
  if (!home.has_value()) return hosts;

  /* known_hosts repeats a host once per key type, so the dedup set keeps the
     scan linear over hundreds of rows. */
  let seen = HashSet{heap_allocator()};
  let const push_unique = [&](StringView host) throws {
    if (host.is_empty() || seen.contains(host)) return;
    seen.add(host);
    hosts.push(String{host});
  };

  let config_path = home->clone();
  config_path.push_component("/.ssh/config");
  if (Maybe<String> config = utils::read_entire_file(config_path.text());
      config.has_value())
  {
    let const text = config->view();
    usize i = 0;
    while (i < text.length) {
      let end = i;
      while (end < text.length && text[end] != '\n')
        end++;
      let row = text.substring_of_length(i, end - i);
      i = end + 1;
      while (!row.is_empty() && (row[0] == ' ' || row[0] == '\t'))
        row = row.substring(1);
      if (!(row.starts_with(StringView{"Host "}) ||
            row.starts_with(StringView{"Host\t"})))
        continue;
      row = row.substring(5);
      /* The Host line lists names separated by blanks, and a name that
         carries a pattern byte is a rule rather than a reachable host. */
      usize k = 0;
      while (k < row.length) {
        while (k < row.length && (row[k] == ' ' || row[k] == '\t'))
          k++;
        let const start = k;
        while (k < row.length && row[k] != ' ' && row[k] != '\t')
          k++;
        let const name = row.substring_of_length(start, k - start);
        if (!name.find_character('*').has_value() &&
            !name.find_character('?').has_value() &&
            !name.find_character('!').has_value())
          push_unique(name);
      }
    }
  }

  let known_hosts_path = home->clone();
  known_hosts_path.push_component("/.ssh/known_hosts");
  if (Maybe<String> known = utils::read_entire_file(known_hosts_path.text());
      known.has_value())
  {
    let const text = known->view();
    usize i = 0;
    while (i < text.length) {
      let end = i;
      while (end < text.length && text[end] != '\n')
        end++;
      let const row = text.substring_of_length(i, end - i);
      i = end + 1;
      /* A hashed row opens with |1| and hides its host on purpose. */
      if (row.is_empty() || row[0] == '#' || row[0] == '|') continue;
      usize field_end = 0;
      while (field_end < row.length && row[field_end] != ' ' &&
             row[field_end] != '\t')
        field_end++;
      let field = row.substring_of_length(0, field_end);
      /* The first field can list host,host and carry a [host]:port form. */
      while (!field.is_empty()) {
        let const comma = field.find_character(',');
        let host =
            comma.has_value() ? field.substring_of_length(0, *comma) : field;
        field = comma.has_value() ? field.substring(*comma + 1) : StringView{};
        if (host.length > 2 && host[0] == '[') {
          let const close = host.find_character(']');
          if (close.has_value()) host = host.substring_of_length(1, *close - 1);
        }
        push_unique(host);
      }
    }
  }
  return hosts;
}

/* Look the tool's targets up in the mtime cache, or rebuild them with the
   given collector and store them under the source file's path. The result
   points into the cache and stays valid until the next refresh, which the
   one synchronous caller below finishes reading before. Null means the
   source file is missing. */
template <typename Collector>
static fn cached_targets_for(const Path &source_file, Collector collect) throws
    -> const ArrayList<String> *
{
  let const mtime = source_file.modification_time();
  if (!mtime.has_value()) return nullptr;
  let const key = source_file.text().view();
  if (const cached_target_list *cached = BUILD_TARGET_CACHE.find(key);
      cached != nullptr && cached->mtime == *mtime)
    return &cached->targets;
  BUILD_TARGET_CACHE.set(key, cached_target_list{*mtime, collect()});
  return &BUILD_TARGET_CACHE.find(key)->targets;
}

/* Complete a build tool's targets and kin, make and ninja targets through
   the tool's own listing, cmake --build targets through its target help,
   package.json script names for the npm family, and ssh hosts from the
   user's ssh files. Subprocesses run only on an explicit tab, and every
   listing caches on the source file's mtime. None lets the cascade
   continue. */
static fn complete_from_build_tools(StringView line, StringView token,
                                    usize token_start, bool for_listing,
                                    EvalContext &context) throws
    -> Maybe<ArrayList<String>>
{
  if (!for_listing) return None;
  if (!token.is_empty() && token[0] == '-') return None;
  let const command = command_word_of(line);
  if (command.is_empty()) return None;

  let const capture = [&](const String &source) throws -> String {
    try {
      return context.capture_command_substitution(source);
    } catch (...) {
      LOG(verbosity::Debug, "swallowed a target listing failure");
      return String{};
    }
  };

  /* The cached branches point straight into the mtime cache, while the ssh
     branch owns its freshly collected list. */
  let owned_targets = ArrayList<String>{};
  const ArrayList<String> *targets = &owned_targets;

  if (command == "make") {
    let const directory =
        settled_option_value(line, "-C").value_or(String{"."});
    let makefile_name = settled_option_value(line, "-f");
    if (!makefile_name.has_value()) {
      /* GNU make reads these three names in this order. */
      for (const StringView candidate :
           {StringView{"GNUmakefile"}, StringView{"makefile"},
            StringView{"Makefile"}})
      {
        let probe = Path{directory.view()};
        probe.push_component(candidate);
        if (probe.exists()) {
          makefile_name = String{candidate};
          break;
        }
      }
      if (!makefile_name.has_value()) return None;
    }
    let makefile_path = Path{directory.view()};
    makefile_path.push_component(makefile_name->view());
    /* A -f naming a file that does not exist, or any path that vanished
       between the probe and now, completes to nothing rather than running
       make against a missing file. */
    if (!makefile_path.exists()) return None;
    targets = cached_targets_for(makefile_path, [&]() throws {
      let invocation = String{"make -C "};
      invocation += directory.view();
      invocation += " -f ";
      invocation += makefile_name->view();
      invocation += " -pRrq : 2>/dev/null";
      return parse_make_database_targets(capture(invocation).view());
    });
  } else if (command == "ninja") {
    let const directory =
        settled_option_value(line, "-C").value_or(String{"."});
    let build_file = Path{directory.view()};
    build_file.push_component(settled_option_value(line, "-f")
                                  .value_or(String{"build.ninja"})
                                  .view());
    targets = cached_targets_for(build_file, [&]() throws {
      let invocation = String{"ninja -C "};
      invocation += directory.view();
      invocation += " -t targets 2>/dev/null";
      return parse_colon_led_names(capture(invocation).view());
    });
  } else if (command == "cmake") {
    /* Only the --target operand of cmake --build completes, through the
       generator's own target help. */
    if (previous_settled_word(line, token_start) != "--target") return None;
    let const build_directory = settled_option_value(line, "--build");
    if (!build_directory.has_value()) return None;
    let cache_file = Path{build_directory->view()};
    cache_file.push_component("CMakeCache.txt");
    targets = cached_targets_for(cache_file, [&]() throws {
      let invocation = String{"cmake --build "};
      invocation += build_directory->view();
      invocation += " --target help 2>/dev/null";
      /* The help lists one "... name" row per target. */
      let names = ArrayList<String>{};
      let const help = capture(invocation);
      let const text = help.view();
      usize i = 0;
      while (i < text.length) {
        let end = i;
        while (end < text.length && text[end] != '\n')
          end++;
        let const row = text.substring_of_length(i, end - i);
        i = end + 1;
        if (!row.starts_with(StringView{"... "})) continue;
        let name = row.substring(4);
        if (let const space = name.find_character(' '); space.has_value())
          name = name.substring_of_length(0, *space);
        if (!name.is_empty()) names.push(String{name});
      }
      return names;
    });
  } else if (command == "npm" || command == "yarn" || command == "pnpm" ||
             command == "bun")
  {
    if (second_word_of(line) != "run") return None;
    let const package_path = Path{StringView{"package.json"}};
    targets = cached_targets_for(package_path, [&]() throws {
      let const contents = utils::read_entire_file(package_path.text());
      return contents.has_value() ? parse_package_json_scripts(contents->view())
                                  : ArrayList<String>{};
    });
  } else if (command == "ssh" || command == "scp") {
    /* The host argument only, so an scp path operand still completes as a
       file. A token that carries / or : is a path or a remote spec. */
    if (token.find_character('/').has_value() ||
        token.find_character(':').has_value())
      return None;
    owned_targets = collect_ssh_hosts();
  } else {
    return None;
  }

  if (targets == nullptr) return None;
  let candidates = ArrayList<String>{};
  for (const String &target : *targets)
    if (target.view().starts_with(token))
      candidates.push(String{target.view()});
  if (candidates.is_empty()) return None;
  return candidates;
}

/* The dash candidates of one builtin, or of the shell binary when the kind
   is None, built once per kind since every source table is immutable and the
   ghost reads these on each keystroke. A builtin's list carries the -x and
   --long forms of its FLAG rows, set's adds its option letters with -o and
   -p from the switch table, and kill's holds the signal names alone the way
   kill -<tab> lists them. Null means the kind registered no flags. */
static fn dash_candidates_for(Maybe<Builtin::Kind> builtin_kind) throws
    -> const ArrayList<String> *
{
  static ArrayList<String> per_kind_candidates[BUILTIN_KIND_COUNT]{};
  static bool per_kind_built[BUILTIN_KIND_COUNT]{};
  static ArrayList<String> binary_candidates{};
  static bool binary_built = false;

  let const append_flag_forms = [](const ArrayList<Flag *> &flags,
                                   ArrayList<String> &out) throws {
    for (const Flag *flag : flags) {
      if (flag->short_name() != '\0') {
        let short_form = String{"-"};
        short_form.push(flag->short_name());
        out.push(steal(short_form));
      }
      if (!flag->long_name().is_empty()) {
        let long_form = String{"--"};
        long_form += flag->long_name();
        out.push(steal(long_form));
      }
    }
  };

  if (!builtin_kind.has_value()) {
    if (!binary_built) {
      append_flag_forms(shit_binary_flag_list(), binary_candidates);
      binary_built = true;
    }
    return &binary_candidates;
  }

  let const index = static_cast<usize>(*builtin_kind);
  if (!per_kind_built[index]) {
    if (*builtin_kind == Builtin::Kind::Kill) {
      for (const StringView name : os::signal_names()) {
        let with_dash = String{"-"};
        with_dash += name;
        per_kind_candidates[index].push(steal(with_dash));
      }
    } else {
      let const flags = builtin_flag_list(*builtin_kind);
      if (flags == nullptr) return nullptr;
      append_flag_forms(*flags, per_kind_candidates[index]);
      if (*builtin_kind == Builtin::Kind::Set) {
        const String &letters = shell_option_letters();
        for (usize i = 0; i < letters.count(); i++) {
          let switch_form = String{"-"};
          switch_form.push(letters[i]);
          per_kind_candidates[index].push(steal(switch_form));
        }
        per_kind_candidates[index].push(String{"-o"});
        per_kind_candidates[index].push(String{"-p"});
      }
    }
    per_kind_built[index] = true;
  }
  return &per_kind_candidates[index];
}

/* Complete a builtin's or the shell binary's own flags from the registered
   FLAG lists, the option names of set and shopt, kill's signal names, and
   kill's %job ids, all table reads with no subprocess so the ghost may run
   it too. None lets the cascade continue. */
static fn complete_from_builtin_flags(StringView line, StringView token,
                                      usize token_start,
                                      EvalContext &context) throws
    -> Maybe<ArrayList<String>>
{
  let const command = command_word_of(line);
  if (command.is_empty()) return None;

  let const builtin_kind = search_builtin(command);
  /* The shell's own invocation completes from its FLAG list, matched by the
     basename so both shit and a path to it answer. */
  let shell_binary_name = command;
  for (usize i = command.length; i > 0; i--)
    if (command[i - 1] == '/') {
      shell_binary_name = command.substring(i);
      break;
    }
  let const completes_shell_binary =
      !builtin_kind.has_value() && shell_binary_name == "shit";
  if (!builtin_kind.has_value() && !completes_shell_binary) return None;

  let candidates = ArrayList<String>{};
  let const push_matching = [&](StringView candidate) throws {
    if (candidate.starts_with(token)) candidates.push(String{candidate});
  };

  /* set -o and set +o name an option by long name, no dash on the operand. */
  if (builtin_kind.has_value() && *builtin_kind == Builtin::Kind::Set) {
    let const previous = previous_settled_word(line, token_start);
    if (previous == "-o" || previous == "+o") {
      for (const StringView name : shell_option_names(true))
        push_matching(name);
      if (!candidates.is_empty()) return candidates;
      return None;
    }
    /* set --mood and set --init-moods take mood names as their value, so the
       operand after either spelling completes the three mood names. */
    if (previous == "--mood" || previous == "-M" || previous == "--init-moods" ||
        previous == "-L")
    {
      for (const StringView name : {StringView{"shit"}, StringView{"bash"},
                                    StringView{"sh"}})
        push_matching(name);
      if (!candidates.is_empty()) return candidates;
      return None;
    }
  }

  /* A shopt operand is an option name, no dash required. */
  if (builtin_kind.has_value() && *builtin_kind == Builtin::Kind::Shopt &&
      (token.is_empty() || token[0] != '-'))
  {
    for (const StringView name : shopt_option_name_list())
      push_matching(name);
    if (!candidates.is_empty()) return candidates;
    return None;
  }

  /* A bare kill operand completes the %job ids, the one live table here. */
  if (builtin_kind.has_value() && *builtin_kind == Builtin::Kind::Kill &&
      (token.is_empty() || token[0] != '-'))
  {
    for (const job &background_job : context.jobs()) {
      let job_id = String{"%"};
      job_id += utils::int_to_text(background_job.id, heap_allocator());
      push_matching(job_id.view());
    }
    if (!candidates.is_empty()) return candidates;
    return None;
  }

  /* Everything below is a flag, so the token must already start the dash. */
  if (token.is_empty() || token[0] != '-') return None;

  const ArrayList<String> *dash_candidates = dash_candidates_for(
      completes_shell_binary ? Maybe<Builtin::Kind>{None} : builtin_kind);
  if (dash_candidates == nullptr) return None;
  for (const String &candidate : *dash_candidates)
    push_matching(candidate.view());
  if (candidates.is_empty()) return None;
  return candidates;
}

/* True when the entry is a dash word the token did not ask for, the one gate
   every spec path applies so an empty argument token completes files rather
   than option words. The caller remembers the drop so a list emptied by it
   falls through to filename completion. */
static pure fn entry_is_unrequested_dash_word(
    StringView entry, bool token_asks_for_dash) wontthrow -> bool
{
  return !token_asks_for_dash && !entry.is_empty() && entry[0] == '-';
}

/* Consult the completion spec registered for the line's command, when one
   exists. The word list filters to the entries that start with the token, and
   the -F function runs only on an explicit tab so the ghost does not run it on
   every keystroke. None means no spec applied, so the caller completes
   filenames, which is also the result when a -o default spec found nothing. */
/* Splits a cobra-style completion entry, the value then two spaces then a
   parenthesized description, into the value and the description. The value
   joins the candidate list and the description joins the same map the --help
   and man stages fill, so every source renders through one dimmed column. A
   plain entry with no such description passes through as the value alone. The
   description opens after a space, so a value that itself holds a parenthesis,
   such as a filename, is left whole. */
static fn push_spec_candidate(StringView entry, ArrayList<String> &candidates,
                              StringMap<String> &descriptions) throws -> void
{
  let const paren = entry.find_character('(');
  if (paren.has_value() && *paren > 0 && entry[*paren - 1] == ' ' &&
      entry[entry.length - 1] == ')')
  {
    let name = entry.substring_of_length(0, *paren);
    while (!name.is_empty() && name[name.length - 1] == ' ')
      name = name.substring_of_length(0, name.length - 1);
    let const description =
        entry.substring_of_length(*paren + 1, entry.length - *paren - 2);
    if (!name.is_empty()) {
      candidates.push(String{name});
      if (!description.is_empty()) descriptions.set(name, String{description});
      return;
    }
  }
  candidates.push(String{entry});
}

static fn complete_from_spec(StringView line, StringView token, usize cursor,
                             bool for_listing, EvalContext &context,
                             StringMap<String> &descriptions) throws
    -> Maybe<ArrayList<String>>
{
  let const command = command_word_of(line);
  if (command.is_empty()) return None;

  /* A cobra-style completion function truncates its description to COLUMNS and
     drops it when the width is too small, so the width is set wide for the
     function run and restored after. The whole description then arrives for
     shit's own dimmed column rather than a truncated parenthesized one. */
  let const saved_columns = context.get_variable_value("COLUMNS");
  context.set_shell_variable("COLUMNS", "100000");
  defer
  {
    if (saved_columns.has_value())
      context.set_shell_variable("COLUMNS", saved_columns->view());
    else
      context.unset_shell_variable("COLUMNS");
  };
  /* The surface name wins when it has a spec of its own, so a complete -F on
     the exact name still applies. Otherwise the name resolves through an
     alias and a symlink, so g for a g='git' alias reads git's spec. */
  const completion_spec *spec = context.lookup_completion_spec(command);
  String resolved_command;
  if (spec == nullptr) {
    resolved_command = resolve_completion_command(command, context);
    if (resolved_command.view() != command)
      spec = context.lookup_completion_spec(resolved_command.view());
  }
  LOG(verbosity::All,
      "spec lookup for '%.*s' %s, listing %d, function '%s', %zu word-list "
      "bytes",
      static_cast<int>(command.length), command.data,
      spec != nullptr ? "hit" : "missed", for_listing ? 1 : 0,
      spec != nullptr ? spec->function_name.c_str() : "",
      spec != nullptr ? spec->word_list.length() : 0);

  /* No command-specific spec. On an explicit tab, consult the default
     completion the way bash-completion's complete -D dynamic loader does. The
     loader sources the per-command file and returns 124 to ask for a retry, so
     the now registered command spec is looked up and run. A non-124 return
     means the default itself produced the candidates. The ghost path never runs
     this since sourcing a file on every keystroke would be wrong. */
  if (spec == nullptr) {
    if (!for_listing) return None;
    const completion_spec *def = context.default_completion_spec();
    if (def == nullptr || def->function_name.is_empty()) return None;
    usize default_cword = 0;
    let const default_words =
        split_completion_words(line, cursor, default_cword);
    i32 status = 0;
    let const reply = context.run_completion_function(
        def->function_name.view(), default_words, default_cword, line, cursor,
        &status);
    if (status != 124) {
      /* The same dash gate the spec paths below apply, so the default
         function's reply offers options only once the token asks for them. */
      let const wants_dash_entries = !token.is_empty() && token[0] == '-';
      let loaded = ArrayList<String>{};
      for (const String &entry : reply) {
        if (entry_is_unrequested_dash_word(entry.view(), wants_dash_entries))
          continue;
        push_spec_candidate(entry.view(), loaded, descriptions);
      }
      /* An empty reply never claims the completion, so the cascade falls to
         the filesystem the way bash-completion's -o default behaves, and a
         cp whose loaded spec offers nothing for an operand still completes
         paths. */
      if (loaded.is_empty()) return None;
      return loaded;
    }
    spec = context.lookup_completion_spec(command);
    if (spec == nullptr) return None;
  }

  let candidates = ArrayList<String>{};

  let const should_offer_dash_words = !token.is_empty() && token[0] == '-';

  if (!spec->word_list.is_empty()) {
    /* The -W list expands the way bash expands it, through the same shared
       path compgen -W reads. The ghost runs on every keystroke and so keeps
       the plain split for a list that would need a parse. */
    for (const String &word :
         context.expand_wordlist_to_fields(spec->word_list.view(), for_listing))
    {
      if (entry_is_unrequested_dash_word(word.view(), should_offer_dash_words))
        continue;
      if (word.view().starts_with(token)) candidates.push(String{word.view()});
    }
  }

  /* The function returns the final candidate list in COMPREPLY, already
     filtered to the current word, so its entries are taken as they are, under
     the same dash gate the word list passes through. */
  if (for_listing && !spec->function_name.is_empty()) {
    usize cword = 0;
    let const words = split_completion_words(line, cursor, cword);
    let const reply = context.run_completion_function(
        spec->function_name.view(), words, cword, line, cursor);
    for (const String &entry : reply) {
      if (entry_is_unrequested_dash_word(entry.view(), should_offer_dash_words))
        continue;
      push_spec_candidate(entry.view(), candidates, descriptions);
    }
  }

  /* An empty candidate set never claims the completion, whatever the spec's
     options say, so a function that replied nothing for an operand falls to
     the filesystem rather than leaving the token dead. */
  if (candidates.is_empty()) return None;
  return candidates;
}

/* One open command substitution while scanning toward the cursor. A $( body and
   a backtick body are tracked the same, the kind only decides which closer ends
   it. */
struct completion_sub_frame
{
  usize body_start;
  bool is_backtick;
};

/* The byte offset where the innermost still-open command substitution body
   begins at the cursor, or zero when the cursor sits outside one. A $( and a
   backtick open a body, a matching ) and the next backtick close it, so
   completion inside echo $(git che re-roots to the inner git line and offers
   git's subcommands rather than the outer command's arguments. An arithmetic
   $(( carries no command, so its body never re-roots, and a single-quoted run
   is literal, so its contents open nothing. */
static fn command_substitution_body_start(StringView line, usize cursor) throws
    -> usize
{
  let frames = ArrayList<completion_sub_frame>{};
  let in_single_quote = false;
  usize i = 0;
  while (i < cursor) {
    let const c = line[i];
    if (in_single_quote) {
      if (c == '\'') in_single_quote = false;
      i++;
      continue;
    }
    if (c == '\\') {
      i += 2;
      continue;
    }
    if (c == '\'') {
      in_single_quote = true;
      i++;
      continue;
    }
    if (c == '`') {
      if (!frames.is_empty() && frames.back().is_backtick)
        frames.pop_back();
      else
        frames.push(completion_sub_frame{i + 1, true});
      i++;
      continue;
    }
    if (c == '$' && i + 1 < cursor && line[i + 1] == '(') {
      if (i + 2 < cursor && line[i + 2] == '(') {
        i += 3;
        continue;
      }
      frames.push(completion_sub_frame{i + 2, false});
      i += 2;
      continue;
    }
    if (c == ')') {
      if (!frames.is_empty() && !frames.back().is_backtick) frames.pop_back();
      i++;
      continue;
    }
    i++;
  }
  return frames.is_empty() ? 0 : frames.back().body_start;
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

  /* A command-position token that names a path component is a program given by
     path rather than a bare command word, so it completes against the
     filesystem the way dash does instead of the command name sets. */
  let const token_has_path_separator = token.find_character('/').has_value();

  TRACELN("complete line '%.*s' cursor %zu token '%.*s' command %d",
          static_cast<int>(line.length), line.data, cursor,
          static_cast<int>(token.length), token.data, is_command ? 1 : 0);

  /* A glob word with the cursor right after it expands inline to its file
     matches, even in command position, the way the shell expands it before
     running a command. */
  let const inline_glob = token_is_glob && cursor == token_end;

  let candidates = ArrayList<String>{};
  /* Filled only by the --help option and subcommand stages, keyed by candidate
     text so it survives the sort below, empty for every other source. */
  let descriptions = StringMap<String>{heap_allocator()};

  /* The POSIX mood keeps completion plain the way dash does. The command
     position still completes command names, and everything else is the
     filesystem, so the variable, tilde-user, manpage, and spec stages are
     for the other moods. */
  let const is_posix_completion = context.mood() == mimic_mood::Posix;

  if (token_is_variable(token) && !is_posix_completion) {
    candidates = complete_variable(token, context);
  } else if (token_is_tilde_user_prefix(token) && !is_posix_completion) {
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
    /* The argument cascade is the mood's own. The builtin flag tables answer
       first in the default and bash moods, since a builtin never has a
       manpage of its own. The default mood then asks the man sources, the
       subcommand index then the option page, then the registered specs, then
       the filesystem. The bash mood is the bash engine alone, specs then
       files, with no man stage. The POSIX mood defers straight to the
       filesystem. */
    Maybe<ArrayList<String>> from_stage = None;
    if (!is_posix_completion)
      from_stage =
          complete_from_builtin_flags(line, token, token_start, context);
    if (!from_stage.has_value() && context.mood() == mimic_mood::Default) {
      from_stage = complete_from_man_subcommands(line, token, token_start,
                                                 for_listing, context);
      if (!from_stage.has_value())
        from_stage = complete_from_help_subcommands(
            line, token, token_start, for_listing, context, descriptions);
      if (!from_stage.has_value())
        from_stage = complete_from_manpage(line, token, for_listing, context,
                                           descriptions);
      if (!from_stage.has_value())
        from_stage =
            complete_from_help(line, token, for_listing, context, descriptions);
      if (!from_stage.has_value())
        from_stage = complete_from_build_tools(line, token, token_start,
                                               for_listing, context);
    }
    if (!from_stage.has_value() && !is_posix_completion)
      from_stage = complete_from_spec(line, token, cursor, for_listing, context,
                                      descriptions);
    if (from_stage.has_value())
      candidates = steal(*from_stage);
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
      steal(candidates),
      steal(descriptions),
      steal(longest_common_prefix),
      token_start + completion_offset,
      token_end + completion_offset,
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
  let home =
      user.is_empty() ? os::get_home_directory() : os::get_home_for_user(user);
  if (!home.has_value()) return None;
  let expanded = home->clone();
  if (slash.has_value()) expanded.push_component(word.substring(*slash + 1));
  return String{expanded.text().view()};
}

/* The PATH search verdicts first_word_resolves caches, keyed by the word and
   dropped when PATH changes. */
static String CACHED_PATH_VERDICT_PATH{};
static StringMap<bool> PATH_SEARCH_VERDICTS{heap_allocator()};

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

  /* Only the PATH search verdict caches, so the highlighter and the ghost
     validation both pay the directory stats once per distinct word per
     session, while a function or an alias defined at the prompt is seen live
     by the checks above. The cache drops when PATH changes. */
  if (environment_path_changed(CACHED_PATH_VERDICT_PATH))
    PATH_SEARCH_VERDICTS.clear();
  if (const bool *verdict = PATH_SEARCH_VERDICTS.find(word)) return *verdict;
  let const resolves = utils::search_program_path(word).count() > 0;
  LOG(verbosity::All, "the path search resolves '%.*s' to %s",
      static_cast<int>(word.length), word.data, resolves ? "yes" : "no");
  PATH_SEARCH_VERDICTS.set(word, resolves);
  return resolves;
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
  return lexer::is_whitespace(c) || c == '\n' || c == '|' || c == '&' ||
         c == ';' || c == '<' || c == '>' || c == '(' || c == ')';
}

/* The byte just past a $ expansion that begins at dollar and stays within end,
   covering $name, the special and positional parameters, and ${...} with
   balanced braces. The $(...) form is handled by the caller so it can recurse
   into the inner command. */
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
     lexer keeps arr[i]=v one assignment word, so the highlighter keeps the next
     word in command position rather than coloring the array name as a command.
   */
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

/* Whether the plain word names a path that exists on disk, a tilde prefix
   expanded the way the evaluator expands it. The highlighter paints such an
   argument cyan, so a real file or directory stands apart from a typo, which
   keeps the default color. */
static fn word_names_existing_path(StringView word) throws -> bool
{
  if (word.is_empty()) return false;
  /* A word that opens with a dash is an option, not a path, so it never reaches
     the filesystem. This keeps a flag-heavy line off a stat per option on every
     keystroke, since the highlighter runs each keystroke. */
  if (word[0] == '-') return false;
  if (word[0] == '~') {
    if (Maybe<String> expanded = expand_command_tilde(word);
        expanded.has_value())
      return Path{expanded->view()}.exists();
    return false;
  }
  return Path{word}.exists();
}

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
    let inner = line.substring_of_length(i + 2, expansion_end - (i + 2) - 1);
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

  let all_digits = true;
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
    let sgr = colors::ansi::CYAN;
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

  let stack = ArrayList<highlight_construct>{bump_allocator(HIGHLIGHT_ARENA)};
  let command_position = true;
  let expecting_in = false;
  let for_variable_pending = false;
  let for_do_expected = false;

  let i = begin;
  while (i < end) {
    let const c = line[i];

    if (c == ' ' || c == '\t' || c == '\n') {
      /* A newline ends a command the way a ';' does, so the next word in a
         multiline edit returns to command position and a keyword such as then,
         do, or else after the newline is recognized rather than colored as an
         argument. */
      if (c == '\n') {
        command_position = true;
        expecting_in = false;
      }
      i++;
      continue;
    }

    if (c == '#') {
      push(i, end, colors::ansi::DIM);
      break;
    }

    /* An operator run, bold so the line's structure stands out from the
       words. A separator or an opener moves the next word back to command
       position, a redirection does not. */
    if (c == '|' || c == '&' || c == ';' || c == '<' || c == '>' || c == '(' ||
        c == ')' || c == '{' || c == '}')
    {
      let const operator_start = i;
      let has_separator = false;
      let has_redirect = false;
      let has_opener = false;
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
      push(operator_start, i, colors::ansi::BOLD);
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
        /* A backtick substitution recurses the same way $(...) does. The close
           is the first unescaped backtick, where a backslash escapes only a
           backtick, a dollar, or another backslash the way the lexer reads a
           backquote, so an escaped backtick does not end the substitution. */
        let const inner_begin = i + 1;
        i++;
        while (i < end && line[i] != '`') {
          if (line[i] == '\\' && i + 1 < end &&
              (line[i + 1] == '`' || line[i + 1] == '$' || line[i + 1] == '\\'))
            i += 2;
          else
            i++;
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
      let is_keyword = true;
      let keyword_ok = true;
      let next_is_command = true;
      let opens_in = false;
      let opens_for_variable = false;
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

      /* A command name. A resolved command is blue the way fish paints one,
         an unresolved one is red. */
      push(word_start, word_end,
           first_word_resolves(word, context) ? colors::ansi::BLUE
                                              : colors::ansi::RED);
      command_position = false;
      continue;
    }

    /* An argument, an expansion-built command, or an assignment prefix. The
       inner spans stand. An assignment prefix keeps the next word in command
       position, an expansion-built command moves past it. A plain argument that
       names an existing path is painted cyan, since a real path stands apart
       from a typo there. */
    if (!command_position && plain && !is_assignment &&
        word_names_existing_path(word))
      push(word_start, word_end, colors::ansi::CYAN);
    for (const highlight_span &inner : word_spans)
      push(inner.start, inner.end, inner.sgr);
    if (command_position && !is_assignment) command_position = false;
  }
}

/* The variable names the line itself introduces, a for or select loop variable
   and a plain NAME= assignment, so the highlighter does not red a reference to
   a name the same line binds. */
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

  let bind_next = false;
  usize i = 0;
  while (i < line.length) {
    while (i < line.length && is_separator(line[i]))
      i++;
    let const start = i;
    while (i < line.length && !is_separator(line[i]))
      i++;
    if (i == start) break;
    let const token = line.substring_of_length(start, i - start);

    if (bind_next) {
      if (is_identifier(token)) known_vars.add(token);
      bind_next = false;
    } else if (Maybe<usize> equals = token.find_character('=');
               equals.has_value() && equals.value() > 0)
    {
      let name = token.substring_of_length(0, equals.value());
      /* An array-element assignment binds the base name, so arr[0]=1 makes arr
         known rather than the invalid name arr[0. */
      if (Maybe<usize> bracket = name.find_character('['); bracket.has_value())
        name = name.substring_of_length(0, bracket.value());
      if (is_identifier(name)) known_vars.add(name);
    }
    bind_next = token == "for" || token == "select";
  }
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
    /* The variables the evaluator synthesizes on read are not in the store, so
       they are added here as set rather than computed, which would advance
       RANDOM or read the clock on a keystroke. IFS and LINENO exist in every
       mode, while the rest are bash-mode only, the way get_variable_value gates
       them, so a POSIX run reds an unset $RANDOM. */
    known_vars.add(StringView{"IFS"});
    known_vars.add(StringView{"LINENO"});
    known_vars.add(StringView{"SHIT_GIT_BRANCH"});
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
