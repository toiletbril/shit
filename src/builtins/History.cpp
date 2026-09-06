/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements history listing, clearing, file reading and writing,
 * appended persistence, operand printing, and accepted compatibility
 * operations for the history builtin.
 */

#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Path.hpp"
#include "../Platform.hpp"
#include "../Toiletline.hpp"
#include "../Trace.hpp"
#include "../Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-c] [-d offset] [-r|-n|-a|-w] [-p arg ...] [count]",
                   "-s [arg ...]");
HELP_DESCRIPTION_DECL(
    "The history builtin lists and maintains the interactive command history.");

FLAG(HISTORY_CLEAR, Bool, 'c', "", "Clear the history list.");
FLAG(HISTORY_DELETE, String, 'd', "", "Delete an event or event range.");
FLAG(HISTORY_APPEND, Bool, 'a', "", "Write the history list to the file.");
FLAG(HISTORY_READ_NEW, Bool, 'n', "", "Read the history file into the list.");
FLAG(HISTORY_READ, Bool, 'r', "", "Read the history file into the list.");
FLAG(HISTORY_WRITE, Bool, 'w', "", "Write the history list to the file.");
FLAG(HISTORY_PRINT, Bool, 'p', "", "Print the operands, storing nothing.");
FLAG(HISTORY_STORE, Bool, 's', "", "Store the operands as a history event.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(History);

namespace koshka {

History::History() = default;

pure fn History::kind() const wontthrow -> Builtin::Kind
{
  return Kind::History;
}

/* A failed history operation names the resolved file. A file whose bytes the
   history format rejects is reported apart from a system failure. The system
   message is taken before the file is read again. */
static fn report_history_file_failure(const ExecContext &ec, EvalContext &cxt,
                                      StringView action) throws -> void
{
  let const system_message = os::last_system_error_message();
  let const path = toiletline::history_path();
  if (!path.has_value()) {
    report_soft_builtin_error(ec, cxt, ec.source_location(),
                              StringView{"Unable to "} + action +
                                  " the history file");
    return;
  }

  let const contents = path->read_entire_file();
  let const is_data_invalid =
      contents.has_value() &&
      !toiletline::is_history_contents_valid(contents->view());
  report_soft_builtin_error(
      ec, cxt, ec.source_location(),
      StringView{"cannot "} + action + " history at '" + path->text().view() +
          "': " +
          (is_data_invalid ? StringView{"the file contains invalid data"}
                           : system_message.view()));
}

/* A file that cannot be examined is kept apart from a file that is empty, so an
   unreadable history is reported instead of printed as an empty list. A path
   that does not resolve or does not exist has nothing to read and is valid. */
enum class history_file_condition : u8
{
  Valid,
  Invalid,
  Unreadable,
};

static fn get_history_file_condition() throws -> history_file_condition
{
  let const path = toiletline::history_path();
  if (!path.has_value()) return history_file_condition::Valid;
  if (!path->exists()) return history_file_condition::Valid;

  let const contents = path->read_entire_file();
  if (!contents.has_value()) return history_file_condition::Unreadable;

  return toiletline::is_history_contents_valid(contents->view())
             ? history_file_condition::Valid
             : history_file_condition::Invalid;
}

/* An appended record needs a leading newline when the target does not end with
   one. Only the last byte is read, because the target holds every event this
   shell has already stored. None reports a file that could not be examined. */
static fn history_target_needs_separator(const Path &target) throws
    -> Maybe<bool>
{
  os::file_status status{};
  if (!os::stat_path_following(target.text().view(), status)) return false;
  if (status.size == 0) return false;

  let const opened =
      os::open_file_descriptor(target.text().view(), os::file_open_mode::Read);
  if (!opened.has_value()) return None;

  let const fd = opened.value();
  char last_byte = '\0';
  let const did_seek = os::seek_descriptor_from_start(fd, status.size - 1);
  let const read_count =
      did_seek ? os::read_fd(fd, &last_byte, 1) : Maybe<usize>{None};
  let const was_closed = os::close_fd(fd);
  if (!read_count.has_value() || *read_count != 1 || !was_closed) return None;

  return last_byte != '\n';
}

static fn print_history_list(const ExecContext &ec, EvalContext &cxt,
                             usize wanted_count) throws -> bool
{
  let const read_events = toiletline::history_events(cxt.scratch_allocator());
  if (!read_events.has_value()) {
    report_history_file_failure(ec, cxt, "read");
    return false;
  }

  let const &events = *read_events;

  /* An empty list still reaches a file the reader accepted, so the file itself
     answers whether the history is empty or damaged. */
  if (events.is_empty() &&
      get_history_file_condition() != history_file_condition::Valid)
  {
    report_history_file_failure(ec, cxt, "read");
    return false;
  }

  usize first_index = 0;
  if (wanted_count != 0 && wanted_count < events.count()) {
    first_index = events.count() - wanted_count;
  }

  let out = String{cxt.scratch_allocator()};
  for (usize i = first_index; i < events.count(); i++) {
    char number_buffer[24];
    let const number =
        utils::int_to_text_into(static_cast<i64>(events[i].number),
                                number_buffer, sizeof(number_buffer));
    out.append_repeated(' ', number.length < 5 ? 5 - number.length : 0);
    out.append(number);
    out += "  ";
    out.append(events[i].command.view());
    out += '\n';
  }
  ec.print_to_stdout(out);

  return true;
}

/* The named file this shell has already imported and how many of its bytes were
   taken. A -n import resumes at that offset, so the same events are not stored
   again. */
struct history_import_state
{
  String path{heap_allocator()};
  usize byte_count{0};
};

static fn get_history_import_state() -> history_import_state &
{
  static history_import_state state;
  return state;
}

static fn append_contents_into_history(EvalContext &cxt,
                                       StringView source_text) throws -> bool
{
  let const backing = toiletline::history_path();
  if (!backing.has_value()) return false;
  let const parent = backing->parent_or_current();
  let lock = os::acquire_process_lock(parent.text().view());
  if (!lock.has_value()) return false;
  defer { os::release_process_lock(lock.take()); };

  let const probed_separator = history_target_needs_separator(*backing);
  if (!probed_separator.has_value()) return false;

  let const needs_separator = *probed_separator;

  let payload = String{cxt.scratch_allocator()};
  let contents = source_text;
  if (needs_separator ||
      (!source_text.is_empty() && source_text[source_text.length - 1] != '\n'))
  {
    payload.reserve(source_text.length + (needs_separator ? 1 : 0) + 1);
    if (needs_separator) payload.push('\n');
    payload.append(source_text);
    if (!source_text.is_empty() && source_text[source_text.length - 1] != '\n')
    {
      payload.push('\n');
    }
    contents = payload.view();
  }

  let const opened = os::open_file_descriptor(backing->text().view(),
                                              os::file_open_mode::Append);
  if (!opened.has_value()) return false;

  let const fd = opened.value();
  let const was_written =
      contents.is_empty() || os::write_all(fd, contents.data, contents.length);
  let const was_closed = os::close_fd(fd);
  return was_written && was_closed;
}

/* The named file this shell last wrote and the newest event it stored there. A
   repeated append resumes above that event so the same events are not stored
   twice, while a different file starts from the whole retained list. */
struct history_append_state
{
  String path{heap_allocator()};
  usize event_number{0};
};

static fn get_history_append_state() -> history_append_state &
{
  static history_append_state state;
  return state;
}

/* A write starts from the retained event list, so KOSH_HISTORY_SIZE bounds what
   reaches the target. A byte copy of the backing file would also carry the
   events the list has already dropped. */
static fn write_history_to_file(EvalContext &cxt, const Path &target,
                                bool should_append) throws -> bool
{
  let &append_state = get_history_append_state();
  if (append_state.path.view() != target.text().view()) {
    append_state.path.clear();
    append_state.path.append(target.text().view());
    append_state.event_number = 0;
  }

  usize newest_number = 0;
  if (let const current_number = toiletline::newest_history_event_number();
      current_number.has_value())
  {
    newest_number = *current_number;
  }

  if (append_state.event_number > newest_number)
    append_state.event_number = 0;

  let const watermark = should_append ? append_state.event_number : 0;

  let const source_path = toiletline::history_path();
  if (source_path.has_value() && source_path->exists() && target.exists() &&
      source_path->is_same_file_as(target))
  {
    append_state.event_number = newest_number;
    return true;
  }

  Maybe<usize> written_above{None};
  if (should_append) written_above = watermark;

  /* A failed read would write an empty payload over the target, so the write
     stops before it destroys the saved history. */
  let const read_events =
      toiletline::history_events(cxt.scratch_allocator(), written_above);
  if (!read_events.has_value()) return false;

  let const &events = *read_events;

  let payload = String{cxt.scratch_allocator()};
  for (usize index = 0; index < events.count(); index++)
    toiletline::encode_history_record(payload, events[index].command.view());

  /* A truncating write is staged beside the target and renamed over it, so a
     failure leaves the saved history in place. */
  if (!should_append) {
    let const parent = target.parent_or_current();
    let const replacement = os::write_to_named_temp_file(
        parent, ".kosh_history_write", payload.view());
    if (!replacement.has_value()) return false;
    defer { unused(os::remove_file(replacement->text().view())); };

    if (!os::rename_path(replacement->text().view(), target.text().view()))
      return false;

    append_state.event_number = newest_number;
    return true;
  }

  bool needs_separator = false;
  if (!payload.is_empty()) {
    let const probed_separator = history_target_needs_separator(target);
    if (!probed_separator.has_value()) return false;

    needs_separator = *probed_separator;
  }

  let const opened = os::open_file_descriptor(target.text().view(),
                                              os::file_open_mode::Append);
  if (!opened.has_value()) return false;

  let const fd = opened.value();

  bool was_written = true;
  if (needs_separator) was_written = os::write_all(fd, "\n", 1);

  if (was_written && !payload.is_empty()) {
    was_written = os::write_all(fd, payload.data(), payload.count());
  }

  let const was_closed = os::close_fd(fd);
  if (!was_written || !was_closed) return false;

  append_state.event_number = newest_number;
  return true;
}

struct history_selection
{
  usize first_index;
  usize last_index;
};

static fn parse_history_selection(
    StringView specification,
    const ArrayList<toiletline::history_event> &events) throws
    -> Maybe<history_selection>
{
  if (events.is_empty()) return None;

  Maybe<usize> separator{None};
  for (usize position = 1; position < specification.length; position++) {
    if (specification[position] != '-') continue;
    separator = position;
    break;
  }

  let const do_resolve = [&](StringView text) throws -> Maybe<usize> {
    let const parsed = utils::parse_decimal_i64(text);
    if (parsed.is_error() || parsed.value() == 0) return None;

    if (parsed.value() < 0) {
      let const index = static_cast<i64>(events.count()) + parsed.value();
      if (index < 0) return None;
      return static_cast<usize>(index);
    }

    let const number = static_cast<usize>(parsed.value());
    for (usize index = 0; index < events.count(); index++)
      if (events[index].number == number) return index;

    return None;
  };

  if (!separator.has_value()) {
    let const index = do_resolve(specification);
    if (!index.has_value()) return None;
    return history_selection{*index, *index};
  }

  let const first_index =
      do_resolve(specification.substring_of_length(0, *separator));
  let const last_index = do_resolve(specification.substring(*separator + 1));
  if (!first_index.has_value() || !last_index.has_value() ||
      *first_index > *last_index)
  {
    return None;
  }

  return history_selection{*first_index, *last_index};
}

fn History::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const args = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  toiletline::set_history_limit(
      cxt.get_history_limit("KOSH_HISTORY_SIZE", 4096));

  let did_maintain_list = false;

  if (FLAG_HISTORY_CLEAR.is_enabled()) {
    LOG(Debug, "history clearing the list");
    if (!toiletline::history_clear()) {
      report_soft_builtin_error(ec, cxt, ec.source_location(),
                                "Unable to clear the history list");
      return 1;
    }

    did_maintain_list = true;
  }

  if (FLAG_HISTORY_READ.is_enabled() || FLAG_HISTORY_READ_NEW.is_enabled()) {
    LOG(Debug, "history reading the file into the list");

    if (args.count() > 1)
      cxt.guard_restricted_path(args[1].view(), ec.arg_location_at(1),
                                restricted_path_use::History);

    if (args.count() > 1) {
      let const source = Path{args[1].view()};
      let const source_text = source.read_entire_file();
      if (!source_text.has_value()) {
        report_soft_builtin_error(
            ec, cxt, ec.arg_location_at(1),
            StringView{"cannot read history from '"} + args[1].view() +
                "': " + os::last_system_error_message(),
            "Pass a readable history file, e.g. `history -r ~/.kosh_history`");
        return 1;
      }
      if (!toiletline::is_history_contents_valid(source_text->view())) {
        report_soft_builtin_error(ec, cxt, ec.arg_location_at(1),
                                  StringView{"cannot read history from '"} +
                                      args[1].view() +
                                      "': the file contains invalid data",
                                  "Pass a text history file");
        return 1;
      }
      let &import_state = get_history_import_state();
      let imported = source_text->view();
      if (FLAG_HISTORY_READ_NEW.is_enabled() &&
          import_state.path.view() == args[1].view() &&
          import_state.byte_count <= imported.length)
      {
        imported = imported.substring(import_state.byte_count);
      }

      /* An empty import still runs, because reading a file establishes a
         missing backing store. */
      if (!append_contents_into_history(cxt, imported)) {
        report_soft_builtin_error(ec, cxt, ec.arg_location_at(1),
                                  "Unable to append the history file");
        return 1;
      }

      import_state.path.clear();
      import_state.path.append(args[1].view());
      import_state.byte_count = source_text->count();
    }

    if (!toiletline::history_read()) {
      report_history_file_failure(ec, cxt, "read");
      return 1;
    }

    did_maintain_list = true;
  }

  if (FLAG_HISTORY_APPEND.is_enabled() || FLAG_HISTORY_WRITE.is_enabled()) {
    LOG(Debug, "history writing the list to the file");

    if (args.count() > 1) {
      cxt.guard_restricted_path(args[1].view(), ec.arg_location_at(1),
                                restricted_path_use::History);
      let const target = Path{args[1].view()};
      if (!write_history_to_file(cxt, target, FLAG_HISTORY_APPEND.is_enabled()))
      {
        report_soft_builtin_error(
            ec, cxt, ec.arg_location_at(1),
            StringView{"cannot write history to '"} + args[1].view() +
                "': " + os::last_system_error_message(),
            "Pass a writable path, e.g. `history -w ~/.kosh_history`");
        return 1;
      }
    } else {
      if (!toiletline::history_write()) {
        report_history_file_failure(ec, cxt, "write");
        return 1;
      }
    }

    did_maintain_list = true;
  }

  if (FLAG_HISTORY_PRINT.is_enabled()) {
    let out = String{cxt.scratch_allocator()};
    for (usize i = 1; i < args.count(); i++) {
      out.append(args[i].view());
      out += '\n';
    }
    ec.print_to_stdout(out);
    did_maintain_list = true;
  }

  if (FLAG_HISTORY_DELETE.is_set()) {
    let const read_events = toiletline::history_events(cxt.scratch_allocator());
    if (!read_events.has_value()) {
      report_history_file_failure(ec, cxt, "read");
      return 1;
    }

    let const &events = *read_events;
    let const selection =
        parse_history_selection(FLAG_HISTORY_DELETE.value(), events);
    if (!selection.has_value()) {
      report_soft_builtin_error(ec, cxt, FLAG_HISTORY_DELETE.value_location(),
                                FLAG_HISTORY_DELETE.value() +
                                    ": history position out of range");
      return 1;
    }

    for (usize index = selection->last_index + 1;
         index-- > selection->first_index;)
    {
      let const &event = events[index];
      if (!toiletline::history_rewrite_event(event.number, event.command.view(),
                                             ""))
      {
        report_soft_builtin_error(ec, cxt, FLAG_HISTORY_DELETE.value_location(),
                                  "Unable to delete the history event");
        return 1;
      }
    }

    did_maintain_list = true;
  }

  if (FLAG_HISTORY_STORE.is_enabled()) {
    if (args.count() > 1) {
      let event = String{cxt.scratch_allocator()};
      for (usize index = 1; index < args.count(); index++) {
        if (index > 1) event.push(' ');
        event.append(args[index].view());
      }

      if (!toiletline::history_append_event(event.view()).has_value()) {
        report_soft_builtin_error(ec, cxt, ec.source_location(),
                                  "Unable to store the history event");
        return 1;
      }
    }

    did_maintain_list = true;
  }

  if (did_maintain_list) return 0;

  usize wanted_count = 0;
  if (args.count() > 1) {
    let const parsed = utils::parse_decimal_i64(args[1].view());
    if (parsed.is_error()) {
      report_soft_builtin_error(ec, cxt, ec.arg_location_at(1),
                                StringView{"'"} + args[1].view() +
                                    "' is not a valid count");
      return 2;
    }

    if (parsed.value() > 0) wanted_count = static_cast<usize>(parsed.value());
  }

  if (!print_history_list(ec, cxt, wanted_count)) return 1;

  return 0;
}

} /* namespace koshka */
