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

/* A name a man page or a --help text lists with the description printed beside
   it. The completion menu shows the description dimmed after the name. */
struct help_entry
{
  String name;
  String description;
};

/* Keeps the entries whose name opens with the token, returns their names, and
   carries each kept description into the menu's descriptions map. */
static fn matches_from_help_entries(const ArrayList<help_entry> &entries,
                                    StringView token,
                                    StringMap<String> &descriptions) throws
    -> ArrayList<String>
{
  let matches = ArrayList<String>{};
  for (let const &entry : entries)
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
   different page name. A general `man COMMAND` scan covers the rest. */
static fn manpage_name_for(StringView command) throws -> String
{
  if (command == "clang++" || command == "c++") return String{"clang"};
  if (command == "g++") return String{"gcc"};
  return String{command};
}

/* The subcommand index scanned out of the man1 directories, a command name
   mapped to the subcommands its dashed pages document, so git-commit.1 makes
   commit a candidate for git. The readdir pass runs once per launch on the
   first explicit tab. */
static StringMap<ArrayList<String>> MAN_SUBCOMMAND_INDEX{heap_allocator()};
/* Every stripped section-1 page name mapped to its full file path, the
   existence gate for the subcommand split and the source the synopsis
   validation reads directly rather than forking man per candidate. */
static StringMap<String> MAN_PAGE_FILE_PATHS{heap_allocator()};
/* The synopsis verdict for each command-subcommand page, read at most once per
   launch and only when the token matches its subcommand. */
static StringMap<bool> MAN_SUBCOMMAND_PAGE_VALID{heap_allocator()};
static bool is_man_subcommand_index_built = false;

/* The man1 directories of the host, the $MANPATH entries when set and the stock
   /usr/local and /usr trees otherwise. An empty $MANPATH segment stands for the
   system defaults at that position, the manpath(1) reading. */
static fn manpage_section1_directories() throws -> ArrayList<Path>
{
  let directories = ArrayList<Path>{};
  let seen_roots = HashSet{heap_allocator()};

  let do_push_man1_of_root = [&](StringView root) {
    if (seen_roots.contains(root)) return;
    seen_roots.add(root);
    let directory = Path{root};
    directory.push_component("man1");
    directories.push(steal(directory));
  };
  let do_push_default_roots = [&]() {
    do_push_man1_of_root("/usr/local/share/man");
    do_push_man1_of_root("/usr/share/man");
  };

  let const manpath = os::get_environment_variable("MANPATH");
  if (!manpath.has_value() || manpath->is_empty()) {
    do_push_default_roots();
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
      do_push_default_roots();
    else
      do_push_man1_of_root(segment);
  }
  return directories;
}

/* The page name with its .1 section suffix and an optional compression tail
   removed, None when the entry is not a section-1 page. */
static pure fn strip_man1_suffix(StringView entry) wontthrow
    -> Maybe<StringView>
{
  let name = entry;
  for (let const tail : {StringView{".gz"}, StringView{".xz"},
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

/* One readdir pass builds the page-name set, a second splits each dashed page
   at its first dash. The tail is a subcommand only when the head page exists
   too, so xdg-open invents no xdg, and a digit-leading version tail is none. */
static fn build_man_subcommand_index() throws -> void
{
  is_man_subcommand_index_built = true;
  for (let const &directory : manpage_section1_directories()) {
    LOG(Info, "scanning man1 directory '%s'", directory.text().c_str());
    let entries = Path::read_directory(directory);
    if (!entries.has_value()) {
      LOG(Debug, "directory '%s' is unreadable, skipping",
          directory.text().c_str());
      continue;
    }
    /* A man1 tree holds thousands of pages, so the page map grows to hold this
       directory's entries up front rather than climbing a rehash chain from
       sixteen slots as each page inserts. */
    MAN_PAGE_FILE_PATHS.reserve(MAN_PAGE_FILE_PATHS.count() + entries->count());
    for (let const &entry : *entries) {
      let const stripped = strip_man1_suffix(entry.view());
      if (!stripped.has_value() || stripped->is_empty()) continue;
      if (MAN_PAGE_FILE_PATHS.find(*stripped) != nullptr) continue;
      let file_path = directory.clone();
      file_path.push_component(entry.view());
      MAN_PAGE_FILE_PATHS.set(*stripped, String{file_path.text().view()});
    }
  }
  /* Candidate order does not matter, complete() sorts, so the pass walks the
     path map itself. */
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
  LOG(Info, "indexed %zu section-1 pages", MAN_PAGE_FILE_PATHS.count());
}

/* The synopsis region of a man page source, located by its .SH or mdoc .Sh
   heading, with roff font escapes stripped and whitespace folded so it reads as
   plain bytes. Empty when the page has no synopsis. */
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

/* Whether the command-subcommand page documents the space-separated form in its
   synopsis, so git-commit.1 opening with `git commit` survives while a
   standalone ssh-keygen.1 does not. An unreadable or compressed page keeps its
   candidate on the head-page rule alone. may_read is false on the ghost path,
   which trusts a cached verdict rather than scan a page on a keystroke. */
static fn man_subcommand_page_is_valid(StringView command,
                                       StringView subcommand,
                                       bool may_read) throws -> bool
{
  let page_name = String{command};
  page_name.push('-');
  page_name.append(subcommand);
  if (let const cached = MAN_SUBCOMMAND_PAGE_VALID.find(page_name.view());
      cached != nullptr)
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
  for (let const tail : {StringView{".gz"}, StringView{".xz"},
                         StringView{".zst"}, StringView{".bz2"}})
    if (path_view.length > tail.length &&
        path_view.substring(path_view.length - tail.length) == tail)
    {
      MAN_SUBCOMMAND_PAGE_VALID.set(page_name.view(), true);
      return true;
    }

  let source = Path{file_path->view()}.read_entire_file();
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
    source = target.read_entire_file();
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

/* Whether the token at token_start is the line's first argument, the slot a
   subcommand completes at. */
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
fn second_word_of(StringView line) wontthrow -> Maybe<StringView>
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
fn complete_from_man_subcommands(StringView line, StringView token,
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
  let const resolved_name =
      resolve_completion_command(surface_command, context);
  let const command = resolved_name.view();

  if (!is_man_subcommand_index_built) {
    if (!for_listing) return None;
    build_man_subcommand_index();
  }

  let const subcommands = MAN_SUBCOMMAND_INDEX.find(command);
  if (subcommands == nullptr || subcommands->is_empty()) return None;

  /* Only the candidates the token matches are validated, so a typo reads no
     page and a command with a hundred subcommand pages does not stall. */
  let matches = ArrayList<String>{};
  for (let const &subcommand : *subcommands)
    if (subcommand.view().starts_with(token) &&
        man_subcommand_page_is_valid(command, subcommand.view(), for_listing))
    {
      matches.push(String{subcommand.view()});
    }
  LOG(Debug, "%zu subcommands of '%.*s' match token '%.*s'", matches.count(),
      static_cast<int>(command.length), command.data,
      static_cast<int>(token.length), token.data);
  if (matches.is_empty()) return None;
  return matches;
}

/* Pulls each dash-word out of an option line's tag part, such as -a and --all
   from `-a, --all`, dropping a trailing =VALUE. Shared by the manpage and the
   --help parsers. */
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

  /* The description for each flag, read from the .TP option blocks, with man's
     wrapped lines joined into the whole description. */
  let descriptions = StringMap<String>{heap_allocator()};
  let pending_flags = ArrayList<String>{};
  usize pending_indent = 0;
  let pending_description = String{};

  let do_finalize_pending = [&]() throws -> void {
    if (pending_flags.is_empty()) return;
    let const desc = trim_blanks(pending_description.view());
    for (let const &flag : pending_flags)
      if (!desc.is_empty() && descriptions.find(flag.view()) == nullptr) {
        descriptions.set(flag.view(), String{desc});
      }
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
      do_finalize_pending();
      continue;
    }

    /* A dashless line at the option's indent or deeper continues the wrapped
       description. */
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
    do_finalize_pending();
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
  do_finalize_pending();

  /* The authoritative flag list is the word scan, with the description attached
     where the block pass found one. */
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

/* The commands shit may fork for their --help text, each mapped to the help
   argument that prints the full list, the allowlist half of the gate. A command
   must appear here and resolve into a trusted directory before its --help runs.
   The value is almost always --help, ffmpeg the exception with --help full. A
   non-plain argument also makes the command read options from --help over a
   manpage. A name longer than sixteen bytes cannot key the packed map. */
static constexpr StaticStringMap<const char *>::entry HELP_ALLOWLIST_ENTRIES[] =
    {
        {SSK("ffmpeg"),            "--help full"},
        {SSK("ffprobe"),           "--help full"},
        {SSK("ffplay"),            "--help full"},
        {SSK("cargo"),             "--help"     },
        {SSK("tailscale"),         "--help"     },
        {SSK("oo"),                "--help"     },
        {SSK("rustup"),            "--help"     },
        {SSK("rustc"),             "--help"     },
        {SSK("rustfmt"),           "--help"     },
        {SSK("go"),                "--help"     },
        {SSK("npm"),               "--help"     },
        {SSK("pnpm"),              "--help"     },
        {SSK("yarn"),              "--help"     },
        {SSK("deno"),              "--help"     },
        {SSK("bun"),               "--help"     },
        {SSK("node"),              "--help"     },
        {SSK("pip"),               "--help"     },
        {SSK("pip3"),              "--help"     },
        {SSK("docker"),            "--help"     },
        {SSK("podman"),            "--help"     },
        {SSK("kubectl"),           "--help"     },
        {SSK("helm"),              "--help"     },
        {SSK("gh"),                "--help"     },
        {SSK("glab"),              "--help"     },
        {SSK("terraform"),         "--help"     },
        {SSK("cmake"),             "--help"     },
        {SSK("ninja"),             "--help"     },
        {SSK("meson"),             "--help"     },
        {SSK("jq"),                "--help"     },
        {SSK("yq"),                "--help"     },
        {SSK("rg"),                "--help"     },
        {SSK("fd"),                "--help"     },
        {SSK("bat"),               "--help"     },
        {SSK("fzf"),               "--help"     },
        {SSK("zig"),               "--help"     },
        {SSK("poetry"),            "--help"     },
        {SSK("pipx"),              "--help"     },
        {SSK("uv"),                "--help"     },
        {SSK("just"),              "--help"     },
        {SSK("hugo"),              "--help"     },
        {SSK("pandoc"),            "--help"     },
        {SSK("delta"),             "--help"     },
        {SSK("dust"),              "--help"     },
        {SSK("starship"),          "--help"     },
        {SSK("gofmt"),             "--help"     },
        {SSK("magick"),            "--help"     },
        {SSK("convert"),           "--help"     },
        /* Compilers and binary tools. */
        {SSK("clang"),             "--help"     },
        {SSK("clang++"),           "--help"     },
        {SSK("gcc"),               "--help"     },
        {SSK("g++"),               "--help"     },
        {SSK("cc"),                "--help"     },
        {SSK("c++"),               "--help"     },
        {SSK("clang-format"),      "--help"     },
        {SSK("clang-tidy"),        "--help"     },
        {SSK("gdb"),               "--help"     },
        {SSK("lldb"),              "--help"     },
        {SSK("objdump"),           "--help"     },
        {SSK("readelf"),           "--help"     },
        {SSK("nm"),                "--help"     },
        {SSK("strip"),             "--help"     },
        {SSK("ar"),                "--help"     },
        {SSK("ld"),                "--help"     },
        {SSK("lld"),               "--help"     },
        {SSK("valgrind"),          "--help"     },
        {SSK("cppcheck"),          "--help"     },
        {SSK("ccache"),            "--help"     },
        {SSK("adb"),               "--help"     },
        {SSK("fastboot"),          "--help"     },
        {SSK("waydroid"),          "--help"     },
        /* Language runtimes and their toolchains. */
        {SSK("python"),            "--help"     },
        {SSK("python3"),           "--help"     },
        {SSK("ruby"),              "--help"     },
        {SSK("perl"),              "--help"     },
        {SSK("lua"),               "--help"     },
        {SSK("php"),               "--help"     },
        {SSK("julia"),             "--help"     },
        {SSK("java"),              "--help"     },
        {SSK("javac"),             "--help"     },
        {SSK("kotlin"),            "--help"     },
        {SSK("kotlinc"),           "--help"     },
        {SSK("scala"),             "--help"     },
        {SSK("dotnet"),            "--help"     },
        {SSK("dart"),              "--help"     },
        {SSK("swift"),             "--help"     },
        {SSK("swiftc"),            "--help"     },
        {SSK("elixir"),            "--help"     },
        {SSK("mix"),               "--help"     },
        {SSK("ocaml"),             "--help"     },
        {SSK("crystal"),           "--help"     },
        {SSK("nim"),               "--help"     },
        {SSK("cabal"),             "--help"     },
        {SSK("stack"),             "--help"     },
        {SSK("opam"),              "--help"     },
        {SSK("ghc"),               "--help"     },
        {SSK("tsc"),               "--help"     },
        {SSK("esbuild"),           "--help"     },
        {SSK("prettier"),          "--help"     },
        {SSK("eslint"),            "--help"     },
        {SSK("biome"),             "--help"     },
        {SSK("vite"),              "--help"     },
        {SSK("ansible"),           "--help"     },
        {SSK("ansible-config"),    "--help"     },
        {SSK("ansible-console"),   "--help"     },
        {SSK("ansible-doc"),       "--help"     },
        {SSK("ansible-galaxy"),    "--help"     },
        {SSK("ansible-inventory"), "--help"     },
        {SSK("ansible-playbook"),  "--help"     },
        {SSK("ansible-pull"),      "--help"     },
        {SSK("ansible-test"),      "--help"     },
        {SSK("ansible-vault"),     "--help"     },
        {SSK("typst"),             "--help"     },
        /* Package managers. */
        {SSK("apt"),               "--help"     },
        {SSK("apt-get"),           "--help"     },
        {SSK("dnf"),               "--help"     },
        {SSK("pacman"),            "--help"     },
        {SSK("zypper"),            "--help"     },
        {SSK("apk"),               "--help"     },
        {SSK("brew"),              "--help"     },
        {SSK("flatpak"),           "--help"     },
        {SSK("snap"),              "--help"     },
        {SSK("nix"),               "--help"     },
        {SSK("conda"),             "--help"     },
        {SSK("mamba"),             "--help"     },
        {SSK("gem"),               "--help"     },
        {SSK("composer"),          "--help"     },
        /* Version control. */
        {SSK("hg"),                "--help"     },
        {SSK("svn"),               "--help"     },
        {SSK("jj"),                "--help"     },
        {SSK("fossil"),            "--help"     },
        /* Modern command-line tools. */
        {SSK("eza"),               "--help"     },
        {SSK("lsd"),               "--help"     },
        {SSK("procs"),             "--help"     },
        {SSK("sd"),                "--help"     },
        {SSK("hyperfine"),         "--help"     },
        {SSK("tokei"),             "--help"     },
        {SSK("watchexec"),         "--help"     },
        {SSK("entr"),              "--help"     },
        {SSK("direnv"),            "--help"     },
        {SSK("zoxide"),            "--help"     },
        {SSK("atuin"),             "--help"     },
        {SSK("broot"),             "--help"     },
        {SSK("btm"),               "--help"     },
        {SSK("gitui"),             "--help"     },
        {SSK("dive"),              "--help"     },
        {SSK("k9s"),               "--help"     },
        {SSK("kubectx"),           "--help"     },
        {SSK("kubens"),            "--help"     },
        {SSK("kustomize"),         "--help"     },
        {SSK("skaffold"),          "--help"     },
        /* Cloud and infrastructure. */
        {SSK("doctl"),             "--help"     },
        {SSK("flyctl"),            "--help"     },
        {SSK("pulumi"),            "--help"     },
        {SSK("packer"),            "--help"     },
        {SSK("vault"),             "--help"     },
        {SSK("consul"),            "--help"     },
        {SSK("nomad"),             "--help"     },
        {SSK("vercel"),            "--help"     },
        /* Network and transfer. */
        {SSK("curl"),              "--help"     },
        {SSK("wget"),              "--help"     },
        {SSK("httpie"),            "--help"     },
        {SSK("xh"),                "--help"     },
        {SSK("aria2c"),            "--help"     },
        {SSK("rsync"),             "--help"     },
        {SSK("rclone"),            "--help"     },
        {SSK("openvpn"),           "--help"     },
        /* Text and data. */
        {SSK("mlr"),               "--help"     },
        {SSK("dasel"),             "--help"     },
        {SSK("gron"),              "--help"     },
        {SSK("fx"),                "--help"     },
        {SSK("xsv"),               "--help"     },
        /* System. */
        {SSK("systemctl"),         "--help"     },
        {SSK("journalctl"),        "--help"     },
        {SSK("nmcli"),             "--help"     },
        {SSK("buildah"),           "--help"     },
        {SSK("skopeo"),            "--help"     },
        {SSK("dedoc"),             "--help"     },
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
  if (let const cached = MANPAGE_OPTION_CACHE.find(page_name);
      cached != nullptr)
    return *cached;
  let parsed_options = ArrayList<help_entry>{};
  /* man forks only when it resolves into a trusted directory, so an alias or a
     planted man is never run. The resolved absolute path runs in place of the
     bare name so PATH cannot reresolve it. An absent or untrusted man caches
     the empty list so the page never forks twice. */
  let const man_paths = utils::search_program_path("man");
  if (man_paths.is_empty() ||
      !command_directory_is_trusted(man_paths[0].text().view()))
  {
    LOG(Debug,
        "skipping the man fork for '%.*s' because man is absent or untrusted",
        static_cast<int>(page_name.length), page_name.data);
    MANPAGE_OPTION_CACHE.set(page_name, steal(parsed_options));
    return *MANPAGE_OPTION_CACHE.find(page_name);
  }
  try {
    let const page = context.capture_command_substitution(
        String{man_paths[0].text().view()} + " " + String{page_name} +
        " 2>/dev/null");
    parsed_options = parse_manpage_option_entries(page.view());
  } catch (...) {
    LOG(Debug, "swallowed a man invocation failure for '%.*s'",
        static_cast<int>(page_name.length), page_name.data);
  }
  MANPAGE_OPTION_CACHE.set(page_name, steal(parsed_options));
  return *MANPAGE_OPTION_CACHE.find(page_name);
}

/* Completes an option token from the command's manpage. Runs only on an
   explicit tab and a dash token, so the ghost never forks man and a plain
   argument completes as a filename. None falls through to the spec and files.
 */
fn complete_from_manpage(StringView line, StringView token, bool for_listing,
                         EvalContext &context,
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
  let const resolved_name =
      resolve_completion_command(surface_command, context);
  let const command = resolved_name.view();

  /* A command that prefers --help reads its options from there rather than the
     manpage, so it skips this stage and the help stage below picks it up. */
  if (command_prefers_help_over_manpage(command)) return None;

  /* git commit -<tab> reads the git-commit page when the index knows it, so the
     options come from the subcommand page rather than the umbrella one. */
  let page_name = manpage_name_for(command);
  if (let const subcommand_word = second_word_of(line);
      subcommand_word.has_value())
  {
    if (!is_man_subcommand_index_built) build_man_subcommand_index();
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

/* The option flags and subcommands a command's --help lists, cached under the
   resolved command name. One fork parses both so the raw text frees after.
   HELP_PARSED records a command that ran so it never forks twice. */
static StringMap<ArrayList<help_entry>> HELP_OPTION_CACHE{heap_allocator()};
static StringMap<ArrayList<help_entry>> HELP_SUBCOMMAND_CACHE{heap_allocator()};
static StringMap<bool> HELP_PARSED{heap_allocator()};

/* Whether the directory the resolved binary sits in is safe to fork for its
   --help text. The check is permission-based, so a user tool directory like
   ~/.cargo/bin is trusted while a world-writable one like /tmp is not. */
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
   command is on the allowlist and resolves into a trusted directory. The
   resolved absolute path runs as the only argv entry, not through a shell, so
   no alias shadows it, and stdin is the null device. The capture is bounded by
   the timeout, and any failure caches the empty string so it never forks
   twice. */
static fn help_text_for(StringView command, StringView subcommand = {}) throws
    -> String
{
  let text = String{};
  /* The allowlist entry carries the help argument, so ffmpeg forks --help full
     rather than the summary-only --help. A command not on the list never
     forks. The allowlist and the trust gate read the base command, a known
     subcommand of an allowlisted command is forked too. */
  let help_argument = HELP_ALLOWLIST.find(command);
  let const paths = utils::search_program_path(command);
  if (help_argument.has_value() && !paths.is_empty() &&
      command_directory_is_trusted(paths[0].text().view()))
  {
    /* argv is the absolute path, then the subcommand chain split on spaces,
       then the help argument split on spaces, so git remote add runs as path,
       remote, add, --help. The chain carries its words space-joined under one
       key. */
    let argv = ArrayList<String>{};
    argv.push(String{paths[0].text().view()});
    {
      usize word_start = 0;
      while (word_start < subcommand.length) {
        while (word_start < subcommand.length && subcommand[word_start] == ' ')
          word_start++;
        let word_end = word_start;
        while (word_end < subcommand.length && subcommand[word_end] != ' ')
          word_end++;

        if (word_end > word_start)
          argv.push(String{subcommand.substring_of_length(
              word_start, word_end - word_start)});
        word_start = word_end;
      }
    }
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
    LOG(Debug, "forking '%.*s' for its --help text",
        static_cast<int>(command.length), command.data);
    if (Maybe<String> output =
            os::capture_program_output(argv, HELP_FORK_TIMEOUT_NANOS);
        output.has_value())
      text = steal(*output);
  }
  return text;
}

/* The dash-options a --help text lists, each paired with the description in the
   column beside it. An option line splits at the first run of two or more
   spaces, every dash-word before it mapping to the one description, a trailing
   =VALUE dropped. */
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

    for (let const &flag : extract_dash_flags(option_part))
      if (!seen.contains(flag.view())) {
        seen.add(flag.view());
        entries.push(help_entry{String{flag.view()}, String{description}});
      }
  }
  return entries;
}

static fn parse_help_subcommands(StringView text) throws
    -> ArrayList<help_entry>;

/* The cache key for a fork, the bare command at the top level and the compound
   "command subcommand" at the second level, the way the man path keys
   command-subcommand. */
static fn help_cache_key(StringView command, StringView subcommand) throws
    -> String
{
  if (subcommand.is_empty()) return String{command};
  let key = String{command};
  key += " ";
  key += subcommand;
  return key;
}

/* Forks the command's --help once, parses both options and subcommands out of
   the one capture, and frees the raw text. HELP_PARSED gates the fork so a
   second tab reads the parsed caches. A subcommand forks "command subcommand
   --help" under the compound key. */
static fn ensure_help_parsed(StringView command,
                             StringView subcommand = {}) throws -> void
{
  let const key = help_cache_key(command, subcommand);
  if (HELP_PARSED.find(key.view()) != nullptr) return;
  let const text = help_text_for(command, subcommand);
  HELP_OPTION_CACHE.set(key.view(), parse_help_option_entries(text.view()));
  HELP_SUBCOMMAND_CACHE.set(key.view(), parse_help_subcommands(text.view()));
  HELP_PARSED.set(key.view(), true);
}

/* The options a command's --help text lists, parsed once and cached. */
static fn help_options_for(StringView command,
                           StringView subcommand = {}) throws
    -> const ArrayList<help_entry> &
{
  ensure_help_parsed(command, subcommand);
  return *HELP_OPTION_CACHE.find(help_cache_key(command, subcommand).view());
}

/* Whether a name reads as a subcommand rather than a description fragment,
   non-empty, opening with a letter or digit, carrying only subcommand bytes. */
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

/* Whether a header line opens a subcommand section, matched case-insensitively
   on a "commands:" or "subcommands:" tail. A bare all-caps header with no
   colon, such as tailscale's "SUBCOMMANDS", also opens one, only when the whole
   line is the single word. */
static fn line_opens_subcommand_section(StringView trimmed) wontthrow -> bool
{
  if (trimmed.is_empty()) return false;
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
  if (trimmed[trimmed.length - 1] == ':')
    return ends_with_ignoring_case(StringView{"commands:"}) ||
           ends_with_ignoring_case(StringView{"subcommands:"});
  /* A colon-less header opens a section only when the whole line is upper-case,
     so a lowercase body line reading "commands" opens none. */
  let const is_all_uppercase = [&]() {
    for (usize i = 0; i < trimmed.length; i++)
      if (trimmed[i] >= 'a' && trimmed[i] <= 'z') return false;
    return true;
  };
  let const equals_ignoring_case = [&](StringView word) {
    return trimmed.length == word.length && ends_with_ignoring_case(word);
  };
  return is_all_uppercase() &&
         (equals_ignoring_case(StringView{"commands"}) ||
          equals_ignoring_case(StringView{"subcommands"}));
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

    /* The name column ends at the first run of two or more spaces, the gap
       before the description. */
    let column_end = trimmed.length;
    for (usize j = 0; j + 1 < trimmed.length; j++)
      if (trimmed[j] == ' ' && trimmed[j + 1] == ' ') {
        column_end = j;
        break;
      }

    let description = StringView{};
    if (column_end < trimmed.length)
      description = trim_blanks(
          trimmed.substring_of_length(column_end, trimmed.length - column_end));

    /* The column lists one or more comma-separated aliases such as `ft, fetch`,
       each a usable subcommand the user may type, so every alias becomes its
       own candidate under the shared description. */
    let const column = trimmed.substring_of_length(0, column_end);
    usize alias_start = 0;
    while (alias_start < column.length) {
      let alias_end = alias_start;
      while (alias_end < column.length && column[alias_end] != ',')
        alias_end++;
      let const alias = trim_blanks(
          column.substring_of_length(alias_start, alias_end - alias_start));
      alias_start = alias_end + 1;

      if (!is_plausible_subcommand_name(alias)) continue;
      if (seen.contains(alias)) continue;

      seen.add(alias);
      subcommands.push(help_entry{String{alias}, String{description}});
    }
  }
  return subcommands;
}

/* The subcommands a command's --help text lists, parsed once and cached. */
static fn help_subcommands_for(StringView command,
                               StringView subcommand = {}) throws
    -> const ArrayList<help_entry> &
{
  ensure_help_parsed(command, subcommand);
  return *HELP_SUBCOMMAND_CACHE.find(
      help_cache_key(command, subcommand).view());
}

/* Whether a word names a subcommand the chain so far lists, so a deeper fork is
   reserved for a parsed subcommand rather than an arbitrary word. The prefix is
   the space-joined chain already walked, empty at the base command. */
static fn is_known_help_subcommand(StringView command,
                                   StringView subcommand_prefix,
                                   StringView word) throws -> bool
{
  for (let const &entry : help_subcommands_for(command, subcommand_prefix))
    if (entry.name.view() == word) return true;
  return false;
}

/* The depth a subcommand chain forks to, the command plus this many settled
   subcommand words. A line past it stops forking, so a deep command line cannot
   fork without bound. */
static constexpr usize MAX_SUBCOMMAND_DEPTH = 4;

/* The deepest valid subcommand chain on the line, the settled words after the
   command that each name a subcommand of the chain built so far, joined with a
   space. The walk stops at the first word that names no known subcommand, at a
   dash-led word, at the token under the cursor, or at MAX_SUBCOMMAND_DEPTH, so
   the fork count is bounded. The returned chain keys the option and subcommand
   caches and splits back into argv at fork time. */
static fn settled_subcommand_chain(StringView resolved_command, StringView line,
                                   usize token_start) throws -> String
{
  let chain = String{};

  /* The word offsets come from the surface command, a view into line, while the
     resolved name keys the subcommand lookups. */
  let const surface_command = command_word_of(line);
  if (surface_command.is_empty()) return chain;

  usize depth_count = 0;
  usize i = static_cast<usize>(surface_command.data - line.data) +
            surface_command.length;

  while (depth_count < MAX_SUBCOMMAND_DEPTH) {
    while (i < line.length && (line[i] == ' ' || line[i] == '\t'))
      i++;
    let const start = i;
    while (i < line.length && line[i] != ' ' && line[i] != '\t')
      i++;

    /* A word that reaches the token under the cursor is the token itself rather
       than a settled subcommand, so the chain ends before it. */
    if (start >= token_start || i > token_start) {
      break;
    }
    if (start == i) break;

    let const word = line.substring_of_length(start, i - start);
    if (word[0] == '-') break;
    if (!is_known_help_subcommand(resolved_command, chain.view(), word)) break;

    if (!chain.is_empty()) chain += " ";
    chain += word;
    depth_count++;
  }

  return chain;
}

/* Completes an option token from the command's --help text, the fallback after
   the manpage stage finds no page. The same explicit-tab and dash-token gates
   hold here. */
fn complete_from_help(StringView line, StringView token, usize token_start,
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
  let const resolved_name = resolve_completion_alias(surface_command, context);

  /* The settled subcommand chain forks "command sub1 sub2 --help" for its own
     options, so git remote add -<tab> reads the add options rather than the
     top-level ones. An empty chain reads the base command options. */
  let const chain =
      settled_subcommand_chain(resolved_name.view(), line, token_start);

  let const &options = help_options_for(resolved_name.view(), chain.view());
  if (options.is_empty()) return None;

  let matches = matches_from_help_entries(options, token, descriptions);
  if (matches.is_empty()) return None;
  return matches;
}

/* Completes a subcommand token from the command's --help text, for a tool such
   as cargo that lists subcommands but has no manpage. The same gates as the
   manpage subcommand stage hold. */
fn complete_from_help_subcommands(StringView line, StringView token,
                                  usize token_start, bool for_listing,
                                  EvalContext &context,
                                  StringMap<String> &descriptions) throws
    -> Maybe<ArrayList<String>>
{
  if (!for_listing) return None;
  if (!token.is_empty() && token[0] == '-') return None;
  if (token.find_character('/').has_value()) return None;
  let const surface_command = command_word_of(line);
  if (surface_command.is_empty() ||
      surface_command.find_character('/').has_value())
    return None;

  /* The alias-only name keeps a multiplexer link such as cargo to rustup at the
     surface name, so the --help fork dispatches on the typed argv[0]. */
  let const resolved_name = resolve_completion_alias(surface_command, context);

  /* The settled subcommand chain forks "command sub1 sub2 --help" for its
     sub-subcommands, so docker compose <tab> lists the compose subcommands. An
     empty chain at the first-argument position lists the base subcommands. */
  let const chain =
      settled_subcommand_chain(resolved_name.view(), line, token_start);
  if (chain.is_empty()) {
    if (!is_first_argument_token(line, token_start)) return None;

    /* A command the man index already lists never forks --help to relist them,
       the man stage is authoritative, the fork is reserved for a tool like
       cargo with no man pages. */
    if (is_man_subcommand_index_built) {
      let const man_subcommands =
          MAN_SUBCOMMAND_INDEX.find(resolved_name.view());
      if (man_subcommands != nullptr && !man_subcommands->is_empty())
        return None;
    }
  }

  let const &subcommands =
      help_subcommands_for(resolved_name.view(), chain.view());
  if (subcommands.is_empty()) return None;

  let matches = matches_from_help_entries(subcommands, token, descriptions);
  if (matches.is_empty()) return None;
  return matches;
}

} /* namespace completion */

} /* namespace shit */
