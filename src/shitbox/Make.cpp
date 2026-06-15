#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Shitbox.hpp"
#include "../Utils.hpp"

/* A deliberately small make. It reads a Makefile, holds the variables and the
   target rules, expands $(NAME) and ${NAME}, and runs each recipe line through
   the shell. There are no pattern rules, no automatic variables, and no
   parallelism. The subset is enough to drive a configure-then-make build on a
   bare system. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-f file] [target ...]");

HELP_DESCRIPTION_DECL(
    "The make utility reads a Makefile, expands its variables, and runs the "
    "recipe of each requested target after its prerequisites. With no target "
    "it builds the first one. It supports simple variables, explicit rules, "
    "and "
    "single-level pattern rules such as %.o: %.c with the automatic variables "
    "$@, $<, and $^. It has no chained implicit rules and no parallelism.");

FLAG(MAKE_FILE, String, 'f', "file",
     "Read the named file instead of Makefile.");
FLAG(MAKE_DIR, String, 'C', "directory",
     "Change to this directory before reading the Makefile.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_SHITBOX_UTIL_FLAGS(Make);

namespace shit {

namespace shitbox {

namespace {

struct make_variable
{
  String name;
  String value;
};

struct make_rule
{
  String target;
  ArrayList<String> prerequisites;
  ArrayList<String> recipe_lines;
};

/* The parsed Makefile, the variables and the rules in source order so the first
   rule is the default goal. A target that holds a % is a pattern rule, kept
   apart so an explicit rule always wins over a pattern. */
struct makefile
{
  ArrayList<make_variable> variables;
  ArrayList<make_rule> rules;
  ArrayList<make_rule> pattern_rules;

  fn find_variable(StringView name) const throws -> const String *
  {
    for (const make_variable &variable : variables)
      if (variable.name == name) return &variable.value;
    return nullptr;
  }

  fn find_rule(StringView target) const throws -> const make_rule *
  {
    for (const make_rule &rule : rules)
      if (rule.target == target) return &rule;
    return nullptr;
  }
};

/* The stem a pattern target matches against a goal, the text the % stands for,
   or None when the goal does not fit the prefix and suffix around the %. So the
   pattern %.o matches foo.o with the stem foo. */
static fn match_pattern(StringView pattern, StringView goal) throws
    -> Maybe<String>
{
  let const percent = pattern.find_character('%');
  if (!percent.has_value()) return None;
  let const prefix = pattern.substring_of_length(0, *percent);
  let const suffix = pattern.substring(*percent + 1);
  if (goal.length < prefix.length + suffix.length) return None;
  if (goal.substring_of_length(0, prefix.length) != prefix) return None;
  if (goal.substring(goal.length - suffix.length) != suffix) return None;
  return String{goal.substring_of_length(
      prefix.length, goal.length - prefix.length - suffix.length)};
}

/* Replace every % in the text with the stem, the way a pattern rule turns %.c
   into the matched source name. */
static fn substitute_stem(StringView text, StringView stem) throws -> String
{
  String out{};
  for (usize i = 0; i < text.length; i++) {
    if (text[i] == '%')
      out += stem;
    else
      out.push(text[i]);
  }
  return out;
}

/* Replace the automatic variables $@, $<, and $^ with the target, the first
   prerequisite, and the whole prerequisite list, so a pattern recipe such as
   $(CC) -c $< -o $@ names the right files. This runs on the raw recipe before
   the $(NAME) expansion, and a $$ escape is carried through untouched so the
   later expansion collapses it to a single $ without the following byte being
   read as an automatic variable. */
static fn substitute_automatic(StringView text, StringView target,
                               StringView first_prereq,
                               StringView all_prereqs) throws -> String
{
  String out{};
  usize i = 0;
  while (i < text.length) {
    if (text[i] == '$' && i + 1 < text.length) {
      let const next = text[i + 1];
      if (next == '$') {
        out += "$$";
        i += 2;
        continue;
      }
      if (next == '@') {
        out += target;
        i += 2;
        continue;
      }
      if (next == '<') {
        out += first_prereq;
        i += 2;
        continue;
      }
      if (next == '^') {
        out += all_prereqs;
        i += 2;
        continue;
      }
    }
    out.push(text[i]);
    i++;
  }
  return out;
}

static fn is_blank(char c) wontthrow -> bool
{
  return c == ' ' || c == '\t' || c == '\r';
}

/* The text with leading and trailing blanks removed, so a value or a name is
   read without its surrounding spaces. */
static fn trim(StringView text) wontthrow -> StringView
{
  usize start = 0;
  usize end = text.length;
  while (start < end && is_blank(text[start]))
    start++;
  while (end > start && is_blank(text[end - 1]))
    end--;
  return text.substring_of_length(start, end - start);
}

/* Split a prerequisite or value list on blanks into its words. */
static fn split_words(StringView text) throws -> ArrayList<String>
{
  ArrayList<String> words{};
  usize i = 0;
  while (i < text.length) {
    while (i < text.length && is_blank(text[i]))
      i++;
    let const start = i;
    while (i < text.length && !is_blank(text[i]))
      i++;
    if (i > start)
      words.push(String{text.substring_of_length(start, i - start)});
  }
  return words;
}

/* Expand $(NAME) and ${NAME} against the makefile variables and the process
   environment, repeating until no reference remains or the depth cap is hit, so
   a variable whose value names another variable resolves too. */
static fn expand(EvalContext &cxt, const makefile &mk, StringView text,
                 usize depth) throws -> String
{
  String result{};
  usize i = 0;
  while (i < text.length) {
    if (text[i] == '$' && i + 1 < text.length &&
        (text[i + 1] == '(' || text[i + 1] == '{'))
    {
      /* The close scan balances nested parentheses so a $(dir $(VAR)) or a
         $(shell cmd $(VAR)) reads to its own close rather than the first one.
       */
      let const open = text[i + 1];
      let const close = open == '(' ? ')' : '}';
      usize j = i + 2;
      usize nesting = 1;
      while (j < text.length) {
        if (text[j] == open)
          nesting++;
        else if (text[j] == close && --nesting == 0)
          break;
        j++;
      }
      let const name = text.substring_of_length(i + 2, j - (i + 2));
      if (name.length > 6 && name.substring_of_length(0, 6) == "shell ") {
        /* The $(shell cmd) function runs the command and substitutes its
           output, the way GNU make reads $(shell uname) or $(shell nproc). The
           argument is expanded first so an inner $(VAR) reaches the command. */
        let const command = expand(cxt, mk, name.substring(6), depth + 1);
        result += cxt.capture_command_substitution(command).view();
      } else if (const String *value = mk.find_variable(name); value != nullptr)
      {
        if (depth < 16)
          result += expand(cxt, mk, value->view(), depth + 1).view();
        else
          result += value->view();
      } else if (Maybe<String> from_env = os::get_environment_variable(name);
                 from_env.has_value())
      {
        result += from_env->view();
      } else if (name == "MAKE") {
        /* $(MAKE) names the make program for a recursive build, so it re-enters
           the bundled make rather than an external one. */
        result += "shitbox make";
      }
      i = j < text.length ? j + 1 : j;
    } else if (text[i] == '$' && i + 1 < text.length && text[i + 1] == '$') {
      result += '$';
      i += 2;
    } else {
      result.push(text[i]);
      i++;
    }
  }
  return result;
}

/* The first ':' that opens a rule, a colon not immediately followed by '=', so
   a ':=' assignment is not mistaken for a rule. None when the line has none. */
static fn rule_colon(StringView line) wontthrow -> Maybe<usize>
{
  for (usize i = 0; i < line.length; i++)
    if (line[i] == ':' && !(i + 1 < line.length && line[i + 1] == '='))
      return i;
  return None;
}

/* Parse one assignment, classifying the operator so := and = set the value, +=
   appends to the existing value, and ?= sets only when the name is unset. */
static fn apply_assignment(makefile &mk, StringView name_part,
                           StringView operator_and_value) throws -> void
{
  StringView value = operator_and_value.substring(1);
  char operator_character = ' ';
  if (!name_part.is_empty()) {
    let const last = name_part[name_part.length - 1];
    if (last == ':' || last == '?' || last == '+') {
      operator_character = last;
      name_part = name_part.substring_of_length(0, name_part.length - 1);
    }
  }

  let const name = trim(name_part);
  let const trimmed_value = trim(value);

  for (make_variable &variable : mk.variables) {
    if (variable.name != name) continue;
    if (operator_character == '?') return;
    if (operator_character == '+') {
      variable.value += " ";
      variable.value += trimmed_value;
    } else {
      variable.value = String{trimmed_value};
    }
    return;
  }
  mk.variables.push(make_variable{String{name}, String{trimmed_value}});
}

static fn parse_makefile(StringView source) throws -> makefile
{
  makefile mk{};
  make_rule *current = nullptr;

  for (const StringView &raw : split_keep_newlines(source)) {
    StringView line = raw;
    if (!line.is_empty() && line[line.length - 1] == '\n')
      line = line.substring_of_length(0, line.length - 1);

    /* A line that starts with a tab is a recipe for the rule above it. The tab
       is dropped and the rest is kept verbatim, since a recipe is shell text
       expanded only at build time. */
    if (!line.is_empty() && line[0] == '\t') {
      if (current != nullptr)
        current->recipe_lines.push(String{line.substring(1)});
      continue;
    }

    /* A comment runs to the end of a non-recipe line. */
    if (let const hash = line.find_character('#'); hash.has_value())
      line = line.substring_of_length(0, *hash);

    let const trimmed = trim(line);
    if (trimmed.is_empty()) {
      current = nullptr;
      continue;
    }

    let const colon = rule_colon(trimmed);
    let const equals = trimmed.find_character('=');
    let const is_rule =
        colon.has_value() && (!equals.has_value() || *colon < *equals);

    if (is_rule) {
      let const targets = trim(trimmed.substring_of_length(0, *colon));
      let const prerequisites = trimmed.substring(*colon + 1);
      /* A line may name several targets sharing one recipe, so each gets its
         own rule with the same prerequisites. A target that holds a % is a
         pattern rule and goes in the pattern list. */
      current = nullptr;
      for (const String &target : split_words(targets)) {
        make_rule rule{};
        rule.target = target.clone();
        rule.prerequisites = split_words(prerequisites);
        if (target.view().find_character('%').has_value()) {
          mk.pattern_rules.push(steal(rule));
          current = &mk.pattern_rules[mk.pattern_rules.count() - 1];
        } else {
          mk.rules.push(steal(rule));
          current = &mk.rules[mk.rules.count() - 1];
        }
      }
      continue;
    }

    if (equals.has_value()) {
      apply_assignment(mk, trimmed.substring_of_length(0, *equals),
                       trimmed.substring(*equals));
      current = nullptr;
    }
  }
  return mk;
}

/* Build one target, its prerequisites first, then its recipe lines through the
   shell. visiting guards against a dependency cycle and built skips a target
   already made. */
static fn build_target(const ExecContext &ec, EvalContext &cxt,
                       const makefile &mk, StringView goal,
                       ArrayList<String> &visiting,
                       ArrayList<String> &built) throws -> void
{
  for (const String &done : built)
    if (done == goal) return;
  for (const String &active : visiting)
    if (active == goal)
      throw Error{"make: dependency cycle at target '" + String{goal} + "'"};

  /* The concrete prerequisites and the recipe come from an explicit rule when
     one names the goal, otherwise from the first pattern rule whose target
     matches and whose source prerequisite already exists or can itself be
     made. */
  const ArrayList<String> *recipe_lines = nullptr;
  ArrayList<String> prerequisites{};

  if (const make_rule *rule = mk.find_rule(goal); rule != nullptr) {
    for (const String &prerequisite : rule->prerequisites) {
      let const expanded = expand(cxt, mk, prerequisite.view(), 0);
      for (const String &word : split_words(expanded.view()))
        prerequisites.push(word.clone());
    }
    recipe_lines = &rule->recipe_lines;
  } else {
    for (const make_rule &pattern : mk.pattern_rules) {
      let const stem = match_pattern(pattern.target.view(), goal);
      if (!stem.has_value()) continue;

      ArrayList<String> candidate{};
      for (const String &prerequisite : pattern.prerequisites) {
        let const substituted = substitute_stem(prerequisite.view(), *stem);
        let const expanded = expand(cxt, mk, substituted.view(), 0);
        for (const String &word : split_words(expanded.view()))
          candidate.push(word.clone());
      }
      /* make chooses a pattern rule only when its prerequisite can be supplied,
         so the first prerequisite must already be a file or be a target with
         its own rule. */
      if (!candidate.is_empty() && !Path{candidate[0].view()}.exists() &&
          mk.find_rule(candidate[0].view()) == nullptr)
        continue;

      prerequisites = steal(candidate);
      recipe_lines = &pattern.recipe_lines;
      break;
    }

    if (recipe_lines == nullptr) {
      if (Path{goal}.exists()) return;
      throw Error{"make: no rule to make target '" + String{goal} + "'"};
    }
  }

  visiting.push(String{goal});
  for (const String &prerequisite : prerequisites)
    build_target(ec, cxt, mk, prerequisite.view(), visiting, built);
  visiting.pop_back();

  /* The automatic variables read the goal, the first prerequisite, and the
     whole prerequisite list joined by spaces. */
  let const first_prereq =
      prerequisites.is_empty() ? StringView{} : prerequisites[0].view();
  String all_prereqs{};
  for (const String &prerequisite : prerequisites) {
    if (!all_prereqs.is_empty()) all_prereqs += ' ';
    all_prereqs += prerequisite.view();
  }

  for (const String &recipe : *recipe_lines) {
    StringView body = recipe.view();
    bool is_silent = false;
    bool should_ignore_errors = false;
    /* A leading @ silences the echo and a leading - ignores a failure, in
       either order, the way make reads the recipe prefixes. */
    while (!body.is_empty() && (body[0] == '@' || body[0] == '-')) {
      if (body[0] == '@') is_silent = true;
      if (body[0] == '-') should_ignore_errors = true;
      body = body.substring(1);
    }

    /* The automatic variables are filled on the raw recipe first, then the
       $(NAME) expansion runs, so a $$ stays an escape and a $@ that the
       expansion would not touch is resolved here. */
    let const with_autos =
        substitute_automatic(body, goal, first_prereq, all_prereqs.view());
    let const command = expand(cxt, mk, with_autos.view(), 0);
    if (command.is_empty()) continue;
    if (!is_silent) ec.print_to_stdout(command + "\n");

    let const status = cxt.run_source(command.view(), "make", true,
                                      ec.source_location(), StringView{"make"});
    if (status != 0 && !should_ignore_errors)
      throw Error{"make: recipe for target '" + String{goal} +
                  "' failed with status " +
                  utils::int_to_text(status, heap_allocator())};
  }

  built.push(String{goal});
}

} /* namespace */

Make::Make() = default;

pure Utility::Kind Make::kind() const wontthrow { return Kind::Make; }

fn Make::execute(const ExecContext &ec, EvalContext &cxt,
                 const ArrayList<String> &args) const throws -> i32
{
  /* The -j parallel flag is accepted and ignored since the build runs
     serially, so a bare -j or a -jN is dropped before flag parsing rather than
     read as a target. */
  ArrayList<String> filtered{};
  for (const String &arg : args) {
    let const text = arg.view();
    bool is_jobs_flag = text == "-j";
    if (!is_jobs_flag && text.length > 2 && text[0] == '-' && text[1] == 'j') {
      is_jobs_flag = true;
      for (usize k = 2; k < text.length; k++)
        if (text[k] < '0' || text[k] > '9') {
          is_jobs_flag = false;
          break;
        }
    }
    if (!is_jobs_flag) filtered.push(arg.clone());
  }

  let const operands = parse_util_operands(FLAG_LIST, filtered);
  defer { reset_flags(FLAG_LIST); };

  SHITBOX_SHOW_HELP_AND_RETURN(ec, args);

  /* -C changes to the directory before the Makefile is read, restored when the
     util returns so a recursive make from a recipe leaves the parent cwd
     unchanged. */
  Maybe<Path> saved_directory;
  if (FLAG_MAKE_DIR.is_set()) {
    saved_directory = Path::current_directory();
    if (Path::set_current_directory(Path{FLAG_MAKE_DIR.value()}).is_error())
      throw Error{"make: cannot change to directory '" +
                  String{FLAG_MAKE_DIR.value()} + "'"};
  }
  defer
  {
    if (saved_directory.has_value())
      static_cast<void>(Path::set_current_directory(*saved_directory));
  };

  String makefile_path{};
  if (FLAG_MAKE_FILE.is_set()) {
    makefile_path = String{FLAG_MAKE_FILE.value()};
  } else if (Path{"Makefile"}.exists()) {
    makefile_path = "Makefile";
  } else if (Path{"makefile"}.exists()) {
    makefile_path = "makefile";
  } else {
    throw Error{"make: no Makefile found"};
  }

  Maybe<String> source = utils::read_entire_file(makefile_path.view());
  if (!source.has_value())
    throw Error{"make: cannot read '" + makefile_path + "'"};

  let const mk = parse_makefile(source->view());

  ArrayList<String> goals{};
  if (operands.is_empty()) {
    if (mk.rules.is_empty())
      throw Error{"make: no targets and no default goal"};
    goals.push(mk.rules[0].target.clone());
  } else {
    for (const String &operand : operands)
      goals.push(operand.clone());
  }

  ArrayList<String> visiting{};
  ArrayList<String> built{};
  for (const String &goal : goals)
    build_target(ec, cxt, mk, goal.view(), visiting, built);

  return 0;
}

} /* namespace shitbox */

} /* namespace shit */
