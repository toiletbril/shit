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

/* The settled word right before the token, so set -o NAME completion sees the
   -o. */
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

/* One tool's cached target list, keyed by its source file's absolute path and
   refreshed when the mtime moves, so a second tab pays no fork. */
struct cached_target_list
{
  i64 mtime;
  ArrayList<String> targets;
};
static StringMap<cached_target_list> BUILD_TARGET_CACHE{heap_allocator()};

/* The value of a -C or -f style option already settled on the line, reading
   both the separated "-C dir" and the attached "-Cdir" spellings. */
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

/* A name that carries a slash, repeats a makefile name, or names a path in the
   make directory is a build artifact rather than a target, so it is dropped. */
static fn make_target_is_artifact(StringView name, const Path &directory) throws
    -> bool
{
  if (name.find_character('/').has_value()) return true;
  if (name == StringView{"GNUmakefile"} || name == StringView{"Makefile"} ||
      name == StringView{"makefile"})
  {
    return true;
  }

  let candidate = directory.clone();
  candidate.push_component(name);
  return candidate.exists();
}

/* The targets of a GNU make database dump, the rule-opening lines between
   "# Files" and "# Finished Make data base". A comment, a dot rule, a recipe
   line, a "# Not a target" disowned rule, and an artifact are skipped. */
static fn parse_make_database_targets(StringView database,
                                      const Path &directory) throws
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
    /* The disowned rule follows its comment immediately, so the first
       non-comment line consumes the flag whatever its shape. */
    if (skip_next_rule) {
      skip_next_rule = false;
      continue;
    }
    if (text[0] == '.' || text[0] == '\t') continue;
    let const colon = text.find_character(':');
    if (!colon.has_value() || *colon == 0) continue;
    let const name = text.substring_of_length(0, *colon);
    if (make_target_is_artifact(name, directory)) continue;
    targets.push(String{name});
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

/* The script names of a package.json "scripts" table, a tolerant scan that
   tracks only strings, escapes, and brace nesting, no JSON machinery. */
static fn parse_package_json_scripts(StringView text) throws
    -> ArrayList<String>
{
  let scripts = ArrayList<String>{};
  let const section = StringView{"\"scripts\""};
  usize at = 0;
  let is_found = false;
  for (; at + section.length <= text.length; at++)
    if (text.substring_of_length(at, section.length) == section) {
      is_found = true;
      break;
    }
  if (!is_found) return scripts;
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
  let const do_push_unique = [&](StringView host) throws {
    if (host.is_empty() || seen.contains(host)) return;
    seen.add(host);
    hosts.push(String{host});
  };

  let config_path = home->clone();
  config_path.push_component("/.ssh/config");
  if (Maybe<String> config = config_path.read_entire_file();
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
        {
          do_push_unique(name);
        }
      }
    }
  }

  let known_hosts_path = home->clone();
  known_hosts_path.push_component("/.ssh/known_hosts");
  if (Maybe<String> known = known_hosts_path.read_entire_file();
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
        do_push_unique(host);
      }
    }
  }
  return hosts;
}

/* Look the tool's targets up in the mtime cache, or rebuild them with the
   collector and store them under the source file's path. The result points into
   the cache. Null means the source file is missing. */
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
fn complete_from_build_tools(StringView line, StringView token,
                             usize token_start, bool for_listing,
                             EvalContext &context) throws
    -> Maybe<ArrayList<String>>
{
  if (!for_listing) return None;
  if (!token.is_empty() && token[0] == '-') return None;
  let const command = command_word_of(line);
  if (command.is_empty()) return None;

  /* A `shitbox make` invocation routes the make utility through the multicall
     dispatcher, so the build tool is the second word when the front-end shitbox
     is the command word. */
  let const tool =
      (command == "shitbox") ? second_word_of(line).value_or(command) : command;

  let const capture = [&](const String &source) throws -> String {
    try {
      return context.capture_command_substitution(source);
    } catch (...) {
      LOG(Debug, "swallowed a target listing failure");
      return String{};
    }
  };

  /* The cached branches point straight into the mtime cache, while the ssh
     branch owns its freshly collected list. */
  let owned_targets = ArrayList<String>{};
  const ArrayList<String> *targets = &owned_targets;

  if (tool == "make") {
    let const directory =
        settled_option_value(line, "-C").value_or(String{"."});
    let makefile_name = settled_option_value(line, "-f");
    if (!makefile_name.has_value()) {
      /* GNU make reads these three names in this order. */
      for (let const candidate :
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
    let const make_directory = Path{directory.view()};
    targets = cached_targets_for(makefile_path, [&]() throws {
      let invocation = String{"make -C "};
      invocation += directory.view();
      invocation += " -f ";
      invocation += makefile_name->view();
      invocation += " -pRrq : 2>/dev/null";
      let database_targets = parse_make_database_targets(
          capture(invocation).view(), make_directory);
      if (!database_targets.is_empty()) return database_targets;
      /* An empty dump means no GNU make answered, so the bundled make parser
         reads the Makefile and resolves its variables the way make would. The
         artifact filter drops a name that names a path on disk, and the seen
         set drops a second rule for the same target so a name lists once. */
      let const intrinsic_targets =
          shitbox::collect_makefile_targets(context, makefile_path);
      let filtered = ArrayList<String>{};
      let seen = HashSet{heap_allocator()};
      for (const String &name : intrinsic_targets) {
        if (make_target_is_artifact(name.view(), make_directory) ||
            seen.contains(name.view()))
          continue;
        seen.add(name.view());
        filtered.push(name.clone());
      }
      return filtered;
    });
  } else if (tool == "ninja") {
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
  } else if (tool == "cmake") {
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
  } else if (tool == "npm" || tool == "yarn" || tool == "pnpm" || tool == "bun")
  {
    if (second_word_of(line) != "run") return None;
    let const package_path = Path{StringView{"package.json"}};
    targets = cached_targets_for(package_path, [&]() throws {
      let const contents = package_path.read_entire_file();
      return contents.has_value() ? parse_package_json_scripts(contents->view())
                                  : ArrayList<String>{};
    });
  } else if (tool == "ssh" || tool == "scp") {
    /* The host argument only, so an scp path operand still completes as a
       file. A token that carries / or : is a path or a remote spec. */
    if (token.find_character('/').has_value() ||
        token.find_character(':').has_value())
    {
      return None;
    }
    owned_targets = collect_ssh_hosts();
  } else {
    return None;
  }

  if (targets == nullptr) return None;
  let candidates = ArrayList<String>{};
  for (let const &target : *targets)
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
  static bool was_per_kind_built[BUILTIN_KIND_COUNT]{};
  static ArrayList<String> binary_candidates{};
  static bool was_binary_built = false;

  let const do_append_flag_forms = [](const ArrayList<Flag *> &flags,
                                      ArrayList<String> &out) throws {
    for (let const flag : flags) {
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
    if (!was_binary_built) {
      do_append_flag_forms(shit_binary_flag_list(), binary_candidates);
      was_binary_built = true;
    }
    return &binary_candidates;
  }

  let const index = static_cast<usize>(*builtin_kind);
  if (!was_per_kind_built[index]) {
    if (*builtin_kind == Builtin::Kind::Kill) {
      for (let const name : os::signal_names()) {
        let with_dash = String{"-"};
        with_dash += name;
        per_kind_candidates[index].push(steal(with_dash));
      }
    } else {
      let const flags = builtin_flag_list(*builtin_kind);
      if (flags == nullptr) return nullptr;
      do_append_flag_forms(*flags, per_kind_candidates[index]);
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
    was_per_kind_built[index] = true;
  }
  return &per_kind_candidates[index];
}

/* Complete a builtin's or the shell binary's own flags from the registered
   FLAG lists, the set and shopt option names, and kill's signal and %job ids,
   all table reads with no subprocess. None lets the cascade continue. */
fn complete_from_builtin_flags(StringView line, StringView token,
                               usize token_start, EvalContext &context) throws
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

  /* The shitbox builtin completes its utility names in the first operand slot
     and the chosen utility's flags after, and a bare utility name completes its
     own flags, all from the registered FLAG_LIST. */
  {
    let const is_shitbox_builtin =
        builtin_kind.has_value() && *builtin_kind == Builtin::Kind::Shitbox;
    Maybe<shitbox::Utility::Kind> util_for_flags;
    bool offer_util_names = false;
    if (is_shitbox_builtin) {
      if (previous_settled_word(line, token_start) == command) {
        if (token.is_empty() || token[0] != '-') offer_util_names = true;
      } else if (let const second = second_word_of(line); second.has_value()) {
        util_for_flags = shitbox::find_util(*second);
      }
    } else if (!completes_shell_binary && context.shitbox()) {
      /* A bare utility name completes its own flags only when the shitbox
         option resolves it as a command, so a plain ls keeps the system ls
         completion when the option is off. */
      util_for_flags = shitbox::find_util(command);
    }

    if (offer_util_names) {
      let names = ArrayList<String>{};
      for (const String &name : shitbox::util_names())
        if (name.view().starts_with(token)) names.push(String{name.view()});
      if (!names.is_empty()) return names;
      return None;
    }

    if (util_for_flags.has_value()) {
      /* A non-dash operand completes files through the cascade. */
      if (token.is_empty() || token[0] != '-') return None;
      let const flags = shitbox::shitbox_util_flag_list(*util_for_flags);
      if (flags == nullptr) return None;
      let forms = ArrayList<String>{};
      for (const Flag *flag : *flags) {
        if (flag->short_name() != '\0') {
          let form = String{"-"};
          form.push(flag->short_name());
          if (form.view().starts_with(token)) forms.push(steal(form));
        }
        if (!flag->long_name().is_empty()) {
          let form = String{"--"};
          form += flag->long_name();
          if (form.view().starts_with(token)) forms.push(steal(form));
        }
      }
      if (forms.is_empty()) return None;
      return forms;
    }
  }

  if (!builtin_kind.has_value() && !completes_shell_binary) return None;

  let candidates = ArrayList<String>{};
  let const do_push_matching = [&](StringView candidate) throws {
    if (candidate.starts_with(token)) candidates.push(String{candidate});
  };

  /* set -o and set +o name an option by long name, no dash on the operand. */
  if (builtin_kind.has_value() && *builtin_kind == Builtin::Kind::Set) {
    let const previous = previous_settled_word(line, token_start);
    if (previous == "-o" || previous == "+o") {
      for (let const name : shell_option_names(true))
        do_push_matching(name);
      if (!candidates.is_empty()) return candidates;
      return None;
    }
    /* set --mood and set --init-moods take mood names as their value, so the
       operand after either spelling completes the three mood names. */
    if (previous == "--mood" || previous == "-M" ||
        previous == "--init-moods" || previous == "-L")
    {
      for (let const name :
           {StringView{"shit"}, StringView{"bash"}, StringView{"sh"}})
        do_push_matching(name);
      if (!candidates.is_empty()) return candidates;
      return None;
    }
  }

  /* A shopt operand is an option name, no dash required. */
  if (builtin_kind.has_value() && *builtin_kind == Builtin::Kind::Shopt &&
      (token.is_empty() || token[0] != '-'))
  {
    for (let const name : shopt_option_name_list())
      do_push_matching(name);
    if (!candidates.is_empty()) return candidates;
    return None;
  }

  /* A bare kill operand completes the %job ids, the one live table here. */
  if (builtin_kind.has_value() && *builtin_kind == Builtin::Kind::Kill &&
      (token.is_empty() || token[0] != '-'))
  {
    for (let const &background_job : context.jobs()) {
      let job_id = String{"%"};
      job_id += utils::int_to_text(background_job.id, heap_allocator());
      do_push_matching(job_id.view());
    }
    if (!candidates.is_empty()) return candidates;
    return None;
  }

  /* A bare unset operand is a variable name with no leading $, completing
     against the same shell and environment names as a $-prefixed reference. */
  if (builtin_kind.has_value() && *builtin_kind == Builtin::Kind::Unset &&
      (token.is_empty() || token[0] != '-'))
  {
    /* unset -f removes a function, the plain and -v forms a variable. The flag
       is read from the words typed before this operand. */
    let unsets_function = false;
    let const prefix = line.substring_of_length(0, token_start);
    usize scan_position = 0;
    while (scan_position < prefix.length) {
      while (scan_position < prefix.length &&
             (prefix[scan_position] == ' ' || prefix[scan_position] == '\t'))
        scan_position++;
      let const word_begin = scan_position;
      while (scan_position < prefix.length && prefix[scan_position] != ' ' &&
             prefix[scan_position] != '\t')
        scan_position++;

      let const arg =
          prefix.substring_of_length(word_begin, scan_position - word_begin);
      if (arg.length >= 2 && arg[0] == '-' && arg[1] != '-' &&
          arg.find_character('f').has_value())
        unsets_function = true;
    }

    let seen = HashSet{heap_allocator()};
    let const do_add_name = [&](StringView name) throws {
      if (!name.starts_with(token)) return;
      if (seen.contains(name)) return;
      seen.add(name);
      candidates.push(String{name});
    };

    if (unsets_function) {
      context.function_names().for_each(
          [&](StringView name) { do_add_name(name); });
    } else {
      context.variable_names().for_each(
          [&](StringView name) { do_add_name(name); });

      for (let const &name : os::environment_names())
        do_add_name(name.view());
    }

    if (!candidates.is_empty()) return candidates;
    return None;
  }

  /* Everything below is a flag, so the token must already start the dash. */
  if (token.is_empty() || token[0] != '-') return None;

  const ArrayList<String> *dash_candidates = dash_candidates_for(
      completes_shell_binary ? Maybe<Builtin::Kind>{None} : builtin_kind);
  if (dash_candidates == nullptr) return None;
  for (let const &candidate : *dash_candidates)
    do_push_matching(candidate.view());
  if (candidates.is_empty()) return None;
  return candidates;
}

/* True when the entry is a dash word the token did not ask for, so an empty
   argument token completes files rather than option words. The caller remembers
   the drop so a list emptied by it falls through to filename completion. */
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

fn complete_from_spec(StringView line, StringView token, usize cursor,
                      bool for_listing, EvalContext &context,
                      StringMap<String> &descriptions) throws
    -> Maybe<ArrayList<String>>
{
  let const command = command_word_of(line);
  if (command.is_empty()) return None;

  /* A cobra-style function truncates its description to COLUMNS, so the width
     is set wide for the run and restored after, the whole description arriving
     for shit's own dimmed column. */
  let const saved_columns = context.get_variable_value("COLUMNS");
  context.set_shell_variable("COLUMNS", "100000");
  defer
  {
    if (saved_columns.has_value())
      context.set_shell_variable("COLUMNS", saved_columns->view());
    else
      context.unset_shell_variable("COLUMNS");
  };
  /* The surface name wins when it has a spec of its own. Otherwise it resolves
     through an alias and a symlink, so g for a g='git' alias reads git's spec.
   */
  const completion_spec *spec = context.lookup_completion_spec(command);
  String resolved_command;
  if (spec == nullptr) {
    resolved_command = resolve_completion_command(command, context);
    if (resolved_command.view() != command)
      spec = context.lookup_completion_spec(resolved_command.view());
  }
  LOG(All,
      "spec lookup for '%.*s' %s, listing %d, function '%s', %zu word-list "
      "bytes",
      static_cast<int>(command.length), command.data,
      spec != nullptr ? "hit" : "missed", for_listing ? 1 : 0,
      spec != nullptr ? spec->function_name.c_str() : "",
      spec != nullptr ? spec->word_list.length() : 0);

  /* No command-specific spec. On an explicit tab, consult the default the way
     bash-completion's complete -D loader does. It sources the per-command file
     and returns 124 to ask for a retry, otherwise it produced the candidates
     itself. The ghost path never runs this. */
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
      for (let const &entry : reply) {
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
    for (let const &word :
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
    for (let const &entry : reply) {
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
fn command_substitution_body_start(StringView line, usize cursor) throws
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

} /* namespace completion */

} /* namespace shit */
