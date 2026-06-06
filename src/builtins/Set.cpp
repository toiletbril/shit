#include "../Builtin.hpp"
#include "../Cli.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"

/* set toggles the shell options and rebinds the positional parameters. An
   option is named by a single letter after a minus to enable it or a plus to
   disable it, or by its long name after -o or +o. The letters and names that
   have no backing behavior yet are accepted without effect. */

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("set [-abCefhmnuvx] [+abCefhmnuvx] [-o name] [+o name] "
                   "[--] [arg ...]");

namespace shit {

namespace {

/* One shell option, named by its letter and its long name. set and get are the
   accessors on EvalContext, both null for an option accepted without effect. */
struct SetOption
{
  char letter;
  StringView name;
  void (EvalContext::*set)(bool);
  bool (EvalContext::*get)() const;
};

const SetOption SET_OPTIONS[] = {
    {'e', "errexit", &EvalContext::set_error_exit, &EvalContext::error_exit},
    {'x', "xtrace", &EvalContext::set_echo_expanded,
     &EvalContext::should_echo_expanded},
    {'u', "nounset", &EvalContext::set_error_unset, &EvalContext::error_unset},
    {'a', "allexport", &EvalContext::set_export_all, &EvalContext::export_all},
    {'C', "noclobber", &EvalContext::set_no_clobber, &EvalContext::no_clobber},
    {'f', "noglob", &EvalContext::set_no_glob, &EvalContext::no_glob},
    {'n', "noexec", &EvalContext::set_no_exec, &EvalContext::no_exec},
    {'m', "monitor", &EvalContext::set_monitor, &EvalContext::monitor},
    {'b', "notify", nullptr, nullptr},
    {'h', "hashall", nullptr, nullptr},
    {'v', "verbose", nullptr, nullptr},
};

const SetOption *
find_option_by_letter(char letter)
{
  for (const SetOption &option : SET_OPTIONS)
    if (option.letter == letter) return &option;
  return nullptr;
}

const SetOption *
find_option_by_name(StringView name)
{
  for (const SetOption &option : SET_OPTIONS)
    if (option.name == name) return &option;
  return nullptr;
}

bool
option_is_on(const EvalContext &cxt, const SetOption &option)
{
  return option.get != nullptr ? (cxt.*(option.get))() : false;
}

/* The reusable command form that set -o and set +o print, one line each. */
String
list_options(const EvalContext &cxt)
{
  String out{};
  for (const SetOption &option : SET_OPTIONS) {
    out += option_is_on(cxt, option) ? "set -o " : "set +o ";
    out += option.name;
    out += '\n';
  }
  return out;
}

} /* namespace */

Set::Set() = default;

Builtin::Kind
Set::kind() const
{
  return Kind::Set;
}

i32
Set::execute(ExecContext &ec, EvalContext &cxt) const
{
  const ArrayList<String> &args = ec.args();

  /* set with no arguments lists the shell variables. */
  if (args.size() == 1) {
    String out{};
    for (const String &assignment : cxt.sorted_variable_assignments()) {
      out += assignment.view();
      out += "\n";
    }
    ec.print_to_stdout(out);
    return 0;
  }

  ArrayList<String> operands{heap_allocator()};
  bool collecting_operands = false;
  bool should_rebind = false;

  for (usize i = 1; i < args.size(); i++) {
    const String &arg = args[i];

    if (collecting_operands) {
      operands.push(String{heap_allocator(), arg});
      continue;
    }

    if (arg == "--") {
      collecting_operands = true;
      should_rebind = true;
      continue;
    }

    /* The long form -o name and +o name names one option, or lists them all
       when no name follows. */
    if (arg == "-o" || arg == "+o") {
      bool enable = arg[0] == '-';
      if (i + 1 >= args.size()) {
        ec.print_to_stdout(list_options(cxt));
        continue;
      }
      const String &name = args[++i];
      const SetOption *option = find_option_by_name(name);
      if (option == nullptr)
        throw Error{"set: '" + std::string{name.c_str(), name.size()} +
                    "' is not a valid option name"};
      if (option->set != nullptr) (cxt.*(option->set))(enable);
      continue;
    }

    /* A minus enables each following letter, a plus disables each one. */
    if (arg.length() > 1 && (arg[0] == '-' || arg[0] == '+')) {
      bool enable = arg[0] == '-';
      for (usize c = 1; c < arg.length(); c++) {
        char letter = arg[c];
        const SetOption *option = find_option_by_letter(letter);
        if (option == nullptr) {
          throw Error{"set: '" + std::string{arg[0]} + std::string{letter} +
                      "' is not a valid option"};
        }
        if (option->set != nullptr) (cxt.*(option->set))(enable);
      }
      continue;
    }

    /* The first non-option argument and everything after it rebinds the
       positional parameters. */
    collecting_operands = true;
    should_rebind = true;
    operands.push(String{heap_allocator(), arg});
  }

  if (should_rebind) cxt.set_positional_params(std::move(operands));

  return 0;
}

} /* namespace shit */
