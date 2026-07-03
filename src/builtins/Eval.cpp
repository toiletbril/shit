#include "../Eval.hpp"

#include "../Builtin.hpp"
#include "../Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[arg ...]");

HELP_DESCRIPTION_DECL(
    "The eval builtin runs its arguments as a command in the current shell.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Eval);

namespace shit {

static fn report_eval_invalid_option(const ExecContext &ec, EvalContext &cxt,
                                     StringView option) throws -> i32
{
  let message = String{cxt.scratch_allocator()};
  message.append(ec.program().view());
  message += ": ";
  message.append(option);
  message += ": invalid option";

  const ErrorWithLocation located{ec.source_location(), message.view()};
  if (const String *source = cxt.current_source(); source != nullptr) {
    show_message(located.to_string(source->view()));
  } else {
    let fallback = String{cxt.scratch_allocator()};
    fallback += "shit: ";
    fallback.append(message.view());
    fallback += "\n";
    print_error(fallback.view());
  }

  let note_message = String{cxt.scratch_allocator()};
  note_message += "Try `";
  note_message.append(ec.program().view());
  note_message += " --help` for more info";
  show_message(Note{note_message.view()}.to_string());

  return 2;
}

Eval::Eval() = default;

pure fn Eval::kind() const wontthrow -> Builtin::Kind { return Kind::Eval; }

fn Eval::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  if (ec.args().count() > 1 && ec.args()[1] == "--help")
    SHOW_BUILTIN_HELP_AND_RETURN(ec);

  /* A leading -- ends eval's option scan, matching bash eval -- "$code". */
  usize first = 1;
  if (ec.args().count() > 1) {
    let const &lead = ec.args()[1];
    if (lead == "--") {
      first = 2;
    } else if (lead.length() >= 2 && lead[0] == '-') {
      let invalid_option = String{cxt.scratch_allocator()};
      invalid_option += lead[0];
      invalid_option += lead[1];
      return report_eval_invalid_option(ec, cxt, invalid_option.view());
    }
  }

  let joined = String{cxt.scratch_allocator()};
  for (usize i = first; i < ec.args().count(); i++) {
    if (i > first) joined += ' ';
    joined.append(ec.args()[i].view());
  }

  if (joined.is_empty()) return 0;

  LOG(Debug, "eval running %zu joined bytes in the current shell",
      joined.length());

  return cxt.run_source(joined, "eval", false, ec.source_location(),
                        StringView{"eval"});
}

} // namespace shit
