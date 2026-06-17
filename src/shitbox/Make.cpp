#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Shitbox.hpp"
#include "../Utils.hpp"

#include <glob.h>

/* A deliberately small make. It reads a Makefile, holds the variables and the
   target rules, expands $(NAME) and ${NAME}, and runs each recipe line through
   the shell. There are no pattern rules, no automatic variables, and no
   parallelism. The subset is enough to drive a configure-then-make build on a
   bare system. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-f file] [target ...]");

HELP_DESCRIPTION_DECL(
    "The make utility reads a Makefile, expands its variables, and runs the "
    "recipe of each requested target after its prerequisites, each recipe line "
    "in its own subshell. With no target it builds the first one. It supports "
    "recursive and := simple variables, += and ?= assignments, explicit rules, "
    "single-level pattern rules such as %.o: %.c with the automatic variables "
    "$@, $<, and $^, the ifeq, ifneq, ifdef, and ifndef conditionals, "
    "backslash "
    "line continuations, and the $(wildcard), $(shell), and $(VAR:a=b) "
    "functions. The -B and -k flags and the -j parallelism flag are accepted "
    "and "
    "ignored, since the build runs serially. It has no chained implicit "
    "rules.");

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
  /* A name to array-index map, so a $(NAME) lookup is one hash probe rather
     than a scan of every variable, which a recipe pays once per reference per
     line. The array keeps the values so a value can still be appended in
     place. */
  StringMap<usize> variable_index{heap_allocator()};

  fn find_variable(StringView name) const throws -> const String *
  {
    if (let const *index = variable_index.find(name))
      return &variables[*index].value;
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

static fn expand(EvalContext &cxt, const makefile &mk, StringView text,
                 usize depth) throws -> String;

/* The files each space-separated glob in the argument matches, joined by single
   spaces, the way GNU make $(wildcard *.cc) lists sources. A pattern that
   matches nothing contributes no words. */
static fn make_wildcard(StringView patterns) throws -> String
{
  let result = String{};
  for (const String &pattern : split_words(patterns)) {
    glob_t glob_result{};
    if (glob(pattern.c_str(), 0, nullptr, &glob_result) == 0) {
      for (usize k = 0; k < glob_result.gl_pathc; k++) {
        if (!result.is_empty()) result += ' ';
        result += glob_result.gl_pathv[k];
      }
    }
    globfree(&glob_result);
  }
  return result;
}

/* A substitution reference $(VAR:pattern=replacement), so $(SRC:%.cc=o/%.o)
   maps each word of VAR through the pattern. A pattern holding a % matches a
   stem the replacement % then carries, and a pattern without one replaces a
   trailing suffix, the $(SRC:.cc=.o) form. None means the name is not a
   substitution reference, since a variable name carries no blank and a function
   argument that holds a colon is read elsewhere. */
static fn try_substitution_reference(EvalContext &cxt, const makefile &mk,
                                     StringView name, usize depth) throws
    -> Maybe<String>
{
  let const colon = name.find_character(':');
  if (!colon.has_value() || *colon == 0) {
    return None;
  }

  let const variable_name = name.substring_of_length(0, *colon);
  if (variable_name.find_character(' ').has_value() ||
      variable_name.find_character('\t').has_value())
  {
    return None;
  }

  let const rest = name.substring(*colon + 1);
  let const equals = rest.find_character('=');
  if (!equals.has_value()) return None;

  let const pattern =
      expand(cxt, mk, rest.substring_of_length(0, *equals), depth + 1);
  let const replacement =
      expand(cxt, mk, rest.substring(*equals + 1), depth + 1);

  let value = String{};
  if (const String *stored = mk.find_variable(variable_name); stored != nullptr)
    value = expand(cxt, mk, stored->view(), depth + 1);
  else if (Maybe<String> from_env = os::get_environment_variable(variable_name);
           from_env.has_value())
    value = steal(*from_env);

  let const has_percent = pattern.view().find_character('%').has_value();
  let out = String{};
  for (const String &word : split_words(value.view())) {
    if (!out.is_empty()) out += ' ';
    if (has_percent) {
      if (let const stem = match_pattern(pattern.view(), word.view());
          stem.has_value())
        out += substitute_stem(replacement.view(), stem->view()).view();
      else
        out += word.view();
    } else if (word.view().length >= pattern.view().length &&
               word.view().substring(word.view().length -
                                     pattern.view().length) == pattern.view())
    {
      out += word.view().substring_of_length(0, word.view().length -
                                                    pattern.view().length);
      out += replacement.view();
    } else {
      out += word.view();
    }
  }
  return out;
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
      } else if (name.length > 9 &&
                 name.substring_of_length(0, 9) == "wildcard ")
      {
        let const patterns = expand(cxt, mk, name.substring(9), depth + 1);
        result += make_wildcard(patterns.view()).view();
      } else if (name.length > 6 && name.substring_of_length(0, 6) == "error ")
      {
        /* $(error text) aborts the build with the message, the way GNU make
           stops on a misconfiguration. */
        throw Error{"The makefile stopped the build with the message '" +
                    expand(cxt, mk, name.substring(6), depth + 1) + "'"};
      } else if (name.length > 8 &&
                 name.substring_of_length(0, 8) == "warning ")
      {
        /* $(warning text) is informational only. */
      } else if (Maybe<String> subst =
                     try_substitution_reference(cxt, mk, name, depth);
                 subst.has_value())
      {
        result += subst->view();
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
      } else if (name == "CXX") {
        /* make predefines CXX and CC, so a Makefile that never assigns them
           still finds a compiler. */
        result += "c++";
      } else if (name == "CC") {
        result += "cc";
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
static fn apply_assignment(EvalContext &cxt, makefile &mk, StringView name_part,
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

  /* A := assignment is immediate, so its right-hand side expands now against
     the values defined so far, the way GNU make evaluates a simple variable. A
     later
     $(NAME) then reads the finished string. Expanding here also breaks the
     self-reference in MAKE := $(MAKE) -j$(shell nproc), since $(MAKE) resolves
     to the make program name before the variable is stored rather than
     recursing on itself to the expansion-depth cap. A plain = stays lazy and
     keeps its raw text. */
  let const value_to_store = operator_character == ':'
                                 ? expand(cxt, mk, trimmed_value, 0)
                                 : String{trimmed_value};

  if (let const *index = mk.variable_index.find(name)) {
    make_variable &variable = mk.variables[*index];
    if (operator_character == '?') return;
    if (operator_character == '+') {
      variable.value += " ";
      variable.value += value_to_store.view();
    } else {
      variable.value = String{value_to_store.view()};
    }
    return;
  }
  mk.variable_index.set(name, mk.variables.count());
  mk.variables.push(make_variable{String{name}, String{value_to_store.view()}});
}

/* Whether the line ends in a backslash that continues it, an odd run of
   trailing backslashes, so a doubled \\ is a literal backslash rather than a
   splice. */
static fn ends_with_continuation(StringView line) wontthrow -> bool
{
  usize backslash_count = 0;
  usize k = line.length;
  while (k > 0 && line[k - 1] == '\\') {
    backslash_count++;
    k--;
  }
  return (backslash_count % 2) == 1;
}

/* The source split into logical lines, with a trailing-backslash continuation
   spliced onto the line it continues, the way GNU make joins a multi-line
   assignment or prerequisite list. The trailing newline of each physical line
   is dropped, and the splice collapses the continuation's leading blanks to one
   space. */
static fn join_continuations(StringView source) throws -> ArrayList<String>
{
  let const physical = split_keep_newlines(source);
  ArrayList<String> logical{};
  usize i = 0;
  while (i < physical.count()) {
    let raw = physical[i];
    if (!raw.is_empty() && raw[raw.length - 1] == '\n')
      raw = raw.substring_of_length(0, raw.length - 1);
    let line = String{raw};

    while (ends_with_continuation(line.view()) && i + 1 < physical.count()) {
      line = String{line.view().substring_of_length(0, line.view().length - 1)};
      i++;
      let next = physical[i];
      if (!next.is_empty() && next[next.length - 1] == '\n')
        next = next.substring_of_length(0, next.length - 1);
      line += ' ';
      line += trim(next);
    }

    logical.push(steal(line));
    i++;
  }
  return logical;
}

/* The first whitespace-delimited word, so a directive such as ifeq is read off
   the front of a line. */
static fn leading_word(StringView text) wontthrow -> StringView
{
  usize start = 0;
  while (start < text.length && is_blank(text[start]))
    start++;
  usize end = start;
  while (end < text.length && !is_blank(text[end]))
    end++;
  return text.substring_of_length(start, end - start);
}

/* The two operands of an ifeq or ifneq, the (a,b) form. The comma split honors
   nested parentheses, and each operand is trimmed, so ifeq ($(MODE), dbg) reads
   the second operand as dbg. False when the line is not the parenthesized form.
 */
static fn split_conditional_arguments(StringView rest, StringView &first,
                                      StringView &second) wontthrow -> bool
{
  rest = trim(rest);
  if (rest.is_empty() || rest[0] != '(') return false;

  usize close = rest.length;
  while (close > 0 && rest[close - 1] != ')')
    close--;
  if (close <= 1) return false;

  let const content = rest.substring_of_length(1, close - 2);
  usize depth = 0;
  Maybe<usize> comma = None;
  for (usize k = 0; k < content.length; k++) {
    if (content[k] == '(')
      depth++;
    else if (content[k] == ')' && depth > 0)
      depth--;
    else if (content[k] == ',' && depth == 0) {
      comma = k;
      break;
    }
  }
  if (!comma.has_value()) return false;

  first = trim(content.substring_of_length(0, *comma));
  second = trim(content.substring(*comma + 1));
  return true;
}

/* Evaluate one conditional directive. ifeq and ifneq compare two expanded
   operands, while ifdef and ifndef test whether a variable holds a non-empty
   value. */
static fn evaluate_conditional(EvalContext &cxt, const makefile &mk,
                               StringView directive, StringView rest) throws
    -> bool
{
  if (directive == "ifdef" || directive == "ifndef") {
    let const name = expand(cxt, mk, trim(rest), 0);
    bool is_defined = false;
    if (const String *stored = mk.find_variable(name.view());
        stored != nullptr && !stored->is_empty())
      is_defined = true;
    else if (Maybe<String> from_env = os::get_environment_variable(name.view());
             from_env.has_value() && !from_env->is_empty())
      is_defined = true;
    return directive == "ifdef" ? is_defined : !is_defined;
  }

  let first = StringView{};
  let second = StringView{};
  if (!split_conditional_arguments(rest, first, second)) return false;
  let const expanded_first = expand(cxt, mk, first, 0);
  let const expanded_second = expand(cxt, mk, second, 0);
  let const is_equal = expanded_first.view() == expanded_second.view();
  return directive == "ifeq" ? is_equal : !is_equal;
}

/* The conditional nesting state, one entry per open ifeq/ifdef. A line is read
   only when every open conditional is in its active branch. */
struct conditional_state
{
  bool is_branch_active;
  bool was_any_branch_taken;
  bool is_parent_active;
};

static fn parse_makefile(EvalContext &cxt, StringView source) throws -> makefile
{
  makefile mk{};
  make_rule *current = nullptr;
  ArrayList<conditional_state> conditionals{};

  let const do_is_active = [&]() -> bool {
    for (const conditional_state &state : conditionals)
      if (!state.is_branch_active) return false;
    return true;
  };

  for (const String &logical : join_continuations(source)) {
    StringView line = logical.view();

    /* A line that starts with a tab is a recipe for the rule above it, kept
       only inside an active conditional branch. The tab is dropped and the rest
       is kept verbatim, since a recipe is shell text expanded only at build
       time. */
    if (!line.is_empty() && line[0] == '\t') {
      if (do_is_active() && current != nullptr)
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

    let const directive = leading_word(trimmed);

    /* The conditional directives gate the lines between them. An inactive
       branch still tracks nested directives so the matching endif pops the
       right one. */
    if (directive == "ifeq" || directive == "ifneq" || directive == "ifdef" ||
        directive == "ifndef")
    {
      let const is_parent_active = do_is_active();
      let const is_taken =
          is_parent_active &&
          evaluate_conditional(cxt, mk, directive,
                               trimmed.substring(directive.length));
      conditionals.push(
          conditional_state{is_taken, is_taken, is_parent_active});
      current = nullptr;
      continue;
    }
    if (directive == "else") {
      if (!conditionals.is_empty()) {
        conditional_state &top = conditionals[conditionals.count() - 1];
        let const rest = trim(trimmed.substring(directive.length));
        let const else_directive = leading_word(rest);
        if (else_directive == "ifeq" || else_directive == "ifneq" ||
            else_directive == "ifdef" || else_directive == "ifndef")
        {
          let const is_taken =
              top.is_parent_active && !top.was_any_branch_taken &&
              evaluate_conditional(cxt, mk, else_directive,
                                   rest.substring(else_directive.length));
          top.is_branch_active = is_taken;
          if (is_taken) top.was_any_branch_taken = true;
        } else {
          top.is_branch_active =
              top.is_parent_active && !top.was_any_branch_taken;
          top.was_any_branch_taken = true;
        }
      }
      current = nullptr;
      continue;
    }
    if (directive == "endif") {
      if (!conditionals.is_empty()) conditionals.pop_back();
      current = nullptr;
      continue;
    }

    /* A line inside an inactive branch is dropped entirely. */
    if (!do_is_active()) {
      current = nullptr;
      continue;
    }

    /* override re-asserts a value, so its prefix is stripped and the assignment
       parses as usual. undefine, unexport, define, and a bare export are not
       modelled, so they are skipped rather than read as a malformed rule. An
       export that prefixes an assignment keeps the assignment. */
    StringView statement = trimmed;
    if (directive == "override")
      statement = trim(statement.substring(directive.length));

    let const statement_word = leading_word(statement);
    if (statement_word == "undefine" || statement_word == "unexport" ||
        statement_word == "define")
    {
      current = nullptr;
      continue;
    }
    if (statement_word == "export") {
      let const after_export = trim(statement.substring(statement_word.length));
      if (after_export.is_empty()) {
        current = nullptr;
        continue;
      }
      statement = after_export;
    }

    let const colon = rule_colon(statement);
    let const equals = statement.find_character('=');
    let const is_rule =
        colon.has_value() && (!equals.has_value() || *colon < *equals);

    if (is_rule) {
      /* The targets and prerequisites expand when the rule is read, so
         $(OUT): $(OBJECTS) names the resolved files the way GNU make does. A
         target holding a % is a pattern rule and goes in the pattern list. */
      let const targets =
          expand(cxt, mk, trim(statement.substring_of_length(0, *colon)), 0);
      let const prerequisites =
          expand(cxt, mk, statement.substring(*colon + 1), 0);
      current = nullptr;
      for (const String &target : split_words(targets.view())) {
        make_rule rule{};
        rule.target = target.clone();
        rule.prerequisites = split_words(prerequisites.view());
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
      apply_assignment(cxt, mk, statement.substring_of_length(0, *equals),
                       statement.substring(*equals));
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
      throw Error{"The target '" + String{goal} +
                  "' is part of a dependency cycle"};

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
      throw Error{"There is no rule to make the target '" + String{goal} + "'"};
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

    /* A recipe runs the way GNU make runs it under /bin/sh, with the strict
       toggles off, so an unmatched glob or an unset variable in a recipe does
       not abort the build the way the default mood would. The prior runtime is
       restored on the way out, including an unwinding throw. */
    let const saved_runtime = runtime_state::capture(cxt);
    runtime_state recipe_runtime = saved_runtime;
    recipe_runtime.failglob = false;
    recipe_runtime.error_unset = false;
    recipe_runtime.are_warnings_enabled = false;
    recipe_runtime.restore(cxt);
    defer { saved_runtime.restore(cxt); };

    /* Each recipe line runs in its own subshell, the way GNU make spawns a
       shell per line. The parentheses also keep the tail-command exec
       optimization from replacing the make process when the recipe is a single
       external command, which would otherwise abandon the remaining recipe
       lines and targets. The newlines guard the closing paren against a
       trailing comment in the line. */
    let subshell_command = String{"(\n"};
    subshell_command += command.view();
    subshell_command += "\n)";
    let const status = cxt.run_source(subshell_command.view(), "make", true,
                                      ec.source_location(), StringView{"make"});
    if (status != 0 && !should_ignore_errors)
      throw Error{"The recipe for the target '" + String{goal} +
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
  /* The parallelism flag -j, the always-make flag -B, and the keep-going flag
     -k change nothing in this serial make, so they are accepted and dropped
     before flag parsing rather than read as a target or rejected. The -jN form
     carries an optional number. */
  ArrayList<String> filtered{};
  for (const String &arg : args) {
    let const text = arg.view();
    bool is_ignored_flag = text == "-j" || text == "-B" ||
                           text == "--always-make" || text == "-k" ||
                           text == "--keep-going";
    if (!is_ignored_flag && text.length > 2 && text[0] == '-' && text[1] == 'j')
    {
      is_ignored_flag = true;
      for (usize k = 2; k < text.length; k++)
        if (text[k] < '0' || text[k] > '9') {
          is_ignored_flag = false;
          break;
        }
    }
    if (!is_ignored_flag) filtered.push(arg.clone());
  }

  /* A recipe's $(MAKE) re-enters this util while the outer call is still on the
     stack, and the flag list is shared, so the inherited -C or -f is cleared
     before parsing. The outer call already read its flags into locals, so the
     reset does not disturb it. */
  reset_flags(FLAG_LIST);
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
      throw Error{"Unable to change to the directory '" +
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
    throw Error{"Unable to find a Makefile in the current directory"};
  }

  Maybe<String> source = utils::read_entire_file(makefile_path.view());
  if (!source.has_value())
    throw Error{"Unable to read the makefile '" + makefile_path + "'"};

  let const mk = parse_makefile(cxt, source->view());

  ArrayList<String> goals{};
  if (operands.is_empty()) {
    if (mk.rules.is_empty())
      throw Error{"The makefile defines no targets and no default goal"};
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
