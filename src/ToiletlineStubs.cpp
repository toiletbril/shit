/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file supplies KOSH_NO_TOILETLINE builds with file-backed noninteractive
 * history and inert implementations of terminal-dependent editor operations.
 */

#include "CLIColors.hpp"
#include "Errors.hpp"
#include "Eval.hpp"
#include "Platform.hpp"
#include "Toiletline.hpp"
#include "Utils.hpp"

#if defined KOSH_NO_TOILETLINE

namespace koshka::internal {

static constexpr usize NO_EDITOR_HISTORY_ENTRY_MAX_BYTE_COUNT = 2048;
static constexpr usize NO_EDITOR_HISTORY_DECODED_MAX_BYTE_COUNT =
    toiletline::HISTORY_RECORD_MAX_DECODED_BYTE_COUNT;
static constexpr u64 HISTORY_HASH_OFFSET_BASIS = 14695981039346656037ull;
static constexpr u64 HISTORY_HASH_PRIME = 1099511628211ull;
static constexpr char NO_EDITOR_HISTORY_FILE[] = ".kosh_history";

struct no_editor_history_state
{
  String loaded_path{heap_allocator()};
  ArrayList<usize> record_byte_offsets{heap_allocator()};
  usize total_count{0};
  usize file_byte_count{0};
  usize trailing_record_start_byte_offset{0};
  usize first_record_index{0};
  os::file_status file_status{};
  u64 file_contents_hash{HISTORY_HASH_OFFSET_BASIS};
  u16 entry_limit{TL_HISTORY_MAX_SIZE};
  bool is_loaded{false};
  bool has_file_status{false};
};

struct history_record_span
{
  usize start_byte_offset;
  usize end_byte_offset;
};

/* Rereading the same bytes cannot repair a malformed record, and it can repair
   a file that grew between the read and the stat. The outcomes are kept apart
   so a retry is spent only on the second case. */
enum class history_scan_outcome : u8
{
  Loaded,
  Invalid,
  Stale,
};

/* A replacement is computed from one exact snapshot of the file. When the file
   no longer holds that snapshot the caller has to start over from fresh
   bytes. */
enum class history_replace_outcome : u8
{
  Replaced,
  Changed,
  Failed,
};

static fn get_no_editor_history_state() -> no_editor_history_state &
{
  static no_editor_history_state state;
  return state;
}

static fn extend_history_contents_hash(u64 hash, StringView contents) -> u64
{
  for (usize byte_offset = 0; byte_offset < contents.length; byte_offset++) {
    hash ^= static_cast<u8>(contents[byte_offset]);
    hash *= HISTORY_HASH_PRIME;
  }

  return hash;
}

static fn update_history_file_status(no_editor_history_state &state,
                                     const Path &path) -> bool
{
  state.has_file_status =
      os::stat_path_following(path.text().view(), state.file_status);
  return state.has_file_status;
}

static fn resolve_no_editor_history_path() -> Maybe<Path>
{
  if (let const override_path =
          os::get_environment_variable("KOSH_HISTORY_FILE");
      override_path.has_value() && !override_path->is_empty())
  {
    return Path{override_path->view()};
  }

  let home = os::get_home_directory();
  if (!home.has_value()) return None;
  let path = home->clone();
  path.push_component(NO_EDITOR_HISTORY_FILE);
  return path;
}

static fn get_history_record_byte_offset(const no_editor_history_state &state,
                                         usize record_index) -> usize
{
  ASSERT(!state.record_byte_offsets.is_empty());
  return state.record_byte_offsets[(state.first_record_index + record_index) %
                                   state.record_byte_offsets.count()];
}

static fn get_history_record_span(const no_editor_history_state &state,
                                  usize record_index) -> history_record_span
{
  ASSERT(record_index < state.record_byte_offsets.count());
  let const start_byte_offset =
      get_history_record_byte_offset(state, record_index);
  let const end_byte_offset =
      record_index + 1 < state.record_byte_offsets.count()
          ? get_history_record_byte_offset(state, record_index + 1)
          : state.trailing_record_start_byte_offset;
  ASSERT(start_byte_offset < end_byte_offset);
  return {start_byte_offset, end_byte_offset};
}

static fn push_history_record_byte_offset(no_editor_history_state &state,
                                          usize byte_offset) -> void
{
  if (state.entry_limit == 0) return;

  if (state.record_byte_offsets.count() < state.entry_limit) {
    state.record_byte_offsets.push(byte_offset);
    return;
  }

  state.record_byte_offsets[state.first_record_index] = byte_offset;
  state.first_record_index =
      (state.first_record_index + 1) % state.record_byte_offsets.count();
}

static fn next_history_record(StringView contents, usize &byte_offset,
                              history_record_span &span, bool &is_valid) -> bool
{
  span.start_byte_offset = byte_offset;
  bool is_escape_pending = false;
  while (byte_offset < contents.length) {
    let const byte = static_cast<u8>(contents[byte_offset]);
    byte_offset++;

    if (is_escape_pending) {
      is_escape_pending = false;
      continue;
    }
    if (byte == '\\') {
      is_escape_pending = true;
      continue;
    }
    if (byte == '\n') {
      span.end_byte_offset = byte_offset;
      return true;
    }
    if (byte == '\r' || byte == '\t' || byte == '\v' || byte == '\f') continue;
    if (byte < 0x20 || byte == 0x7f) {
      is_valid = false;
      return false;
    }
  }

  return false;
}

static fn decode_history_record(String &decoded, StringView contents,
                                history_record_span span) -> bool
{
  decoded.clear();
  let const encoded_byte_count = span.end_byte_offset - span.start_byte_offset;
  decoded.reserve(encoded_byte_count < NO_EDITOR_HISTORY_DECODED_MAX_BYTE_COUNT
                      ? encoded_byte_count
                      : NO_EDITOR_HISTORY_DECODED_MAX_BYTE_COUNT);
  bool is_escape_pending = false;
  for (usize byte_offset = span.start_byte_offset;
       byte_offset + 1 < span.end_byte_offset; byte_offset++)
  {
    let byte = contents[byte_offset];
    if (is_escape_pending) {
      is_escape_pending = false;
      if (byte == 'n')
        byte = '\n';
      else if (byte != '\\') {
        if (decoded.count() == NO_EDITOR_HISTORY_DECODED_MAX_BYTE_COUNT)
          return false;
        decoded.push('\\');
      }
    } else if (byte == '\\') {
      is_escape_pending = true;
      continue;
    } else if (byte == '\r' && byte_offset + 2 == span.end_byte_offset) {
      /* The record was written with CRLF endings. A carriage return anywhere
         else is entry data. */
      continue;
    }

    if (decoded.count() == NO_EDITOR_HISTORY_DECODED_MAX_BYTE_COUNT)
      return false;
    decoded.push(byte);
  }

  return true;
}

static constexpr usize HISTORY_SPAN_READ_CHUNK_BYTE_COUNT = 512;

/* Reads the encoded bytes of one record. A caller that needs a single record
   leaves the rest of the file unread. The returned text begins at the start of
   the span. Its own span is zero based. */
static fn read_no_editor_history_span(const Path &path,
                                      history_record_span span) throws
    -> Maybe<String>
{
  let const byte_count = span.end_byte_offset - span.start_byte_offset;
  let opened =
      os::open_file_descriptor(path.text().view(), os::file_open_mode::Read);
  if (!opened.has_value()) return None;

  let const fd = opened.value();
  defer { unused(os::close_fd(fd)); };

  if (!os::seek_descriptor_from_start(fd, span.start_byte_offset)) return None;

  let record = String{heap_allocator()};
  record.reserve(byte_count);

  char chunk[HISTORY_SPAN_READ_CHUNK_BYTE_COUNT];
  usize read_byte_count = 0;
  while (read_byte_count < byte_count) {
    let const wanted_byte_count = byte_count - read_byte_count;
    let const result = os::read_fd(
        fd, chunk,
        wanted_byte_count < sizeof chunk ? wanted_byte_count : sizeof chunk);
    if (!result.has_value() || *result == 0) return None;

    record.append(StringView{chunk, *result});
    read_byte_count += *result;
  }

  return record;
}

static fn scan_no_editor_history(const Path &path, StringView contents,
                                 Maybe<u64> contents_hash = None,
                                 bool should_allow_missing = false)
    -> history_scan_outcome
{
  let &state = get_no_editor_history_state();
  if (state.loaded_path.view() != path.text().view())
    state.loaded_path = String{heap_allocator(), path.text().view()};
  state.record_byte_offsets.clear();
  state.total_count = 0;
  state.file_byte_count = contents.length;
  state.trailing_record_start_byte_offset = 0;
  state.first_record_index = 0;
  state.file_contents_hash =
      contents_hash.has_value()
          ? *contents_hash
          : extend_history_contents_hash(HISTORY_HASH_OFFSET_BASIS, contents);
  state.is_loaded = false;
  state.has_file_status = false;

  usize byte_offset = 0;
  bool is_valid = true;
  history_record_span span{};
  while (next_history_record(contents, byte_offset, span, is_valid)) {
    push_history_record_byte_offset(state, span.start_byte_offset);
    state.total_count++;
  }
  if (!is_valid) return history_scan_outcome::Invalid;

  state.trailing_record_start_byte_offset = span.start_byte_offset;
  if (!update_history_file_status(state, path)) {
    if (!should_allow_missing || path.exists())
      return history_scan_outcome::Stale;
  } else if (state.file_status.size != contents.length) {
    return history_scan_outcome::Stale;
  }
  state.is_loaded = true;
  return history_scan_outcome::Loaded;
}

static fn load_no_editor_history(const Path &path, bool should_allow_missing)
    -> ErrorOr<Ok>
{
  let &state = get_no_editor_history_state();
  state.is_loaded = false;

  for (int attempt_index = 0;
       attempt_index < toiletline::HISTORY_RACE_ATTEMPT_COUNT; attempt_index++)
  {
    let const contents = path.read_entire_file();
    if (!contents.has_value()) {
      let const was_missing = os::last_system_error_is_missing_file();
      if (!should_allow_missing || !was_missing)
        return Error{os::last_system_error_message()};

      if (scan_no_editor_history(path, {}, None, true) ==
          history_scan_outcome::Loaded)
      {
        return Success;
      }

      continue;
    }

    let const outcome = scan_no_editor_history(path, contents->view());
    if (outcome == history_scan_outcome::Loaded) return Success;
    if (outcome == history_scan_outcome::Invalid)
      return Error{"the file contains invalid data"};
  }

  return Error{"the file kept changing"};
}

static fn ensure_no_editor_history_loaded(const Path &path,
                                          bool should_allow_missing)
    -> ErrorOr<Ok>
{
  let &state = get_no_editor_history_state();
  if (!state.is_loaded || state.loaded_path.view() != path.text().view())
    return load_no_editor_history(path, should_allow_missing);

  let status = os::file_status{};
  if (!os::stat_path_following(path.text().view(), status)) {
    if (!should_allow_missing || path.exists())
      return Error{os::last_system_error_message()};

    return load_no_editor_history(path, true);
  }
  if (state.has_file_status &&
      os::file_status_matches(state.file_status, status))
  {
    return Success;
  }

  return load_no_editor_history(path, should_allow_missing);
}

static fn read_no_editor_history_contents(const Path &path,
                                          bool should_allow_missing)
    -> ErrorOr<String>
{
  let &state = get_no_editor_history_state();
  let failure_message = String{heap_allocator()};
  for (int attempt_index = 0;
       attempt_index < toiletline::HISTORY_RACE_ATTEMPT_COUNT; attempt_index++)
  {
    let contents = path.read_entire_file();
    if (!contents.has_value()) {
      let const was_missing = os::last_system_error_is_missing_file();
      if (should_allow_missing && was_missing &&
          scan_no_editor_history(path, {}, None, true) ==
              history_scan_outcome::Loaded)
      {
        return String{heap_allocator()};
      }

      if (!was_missing || !should_allow_missing) {
        failure_message = os::last_system_error_message();
        break;
      }

      continue;
    }

    let const contents_hash = extend_history_contents_hash(
        HISTORY_HASH_OFFSET_BASIS, contents->view());
    if (state.is_loaded && state.loaded_path.view() == path.text().view() &&
        state.file_byte_count == contents->count() &&
        state.file_contents_hash == contents_hash)
    {
      return contents.take();
    }

    let const outcome =
        scan_no_editor_history(path, contents->view(), contents_hash);
    if (outcome == history_scan_outcome::Loaded) return contents.take();
    if (outcome == history_scan_outcome::Invalid) {
      failure_message = "the file contains invalid data";
      break;
    }
  }

  state.record_byte_offsets.clear();
  state.total_count = 0;
  state.is_loaded = false;
  if (failure_message.is_empty()) return Error{"the file kept changing"};

  return Error{failure_message.view()};
}

static fn replace_history_file(const Path &path, StringView original,
                               StringView replacement, StringView prefix)
    -> history_replace_outcome
{
  let const parent = path.parent_or_current();
  let replacement_path =
      os::write_to_named_temp_file(parent, prefix, replacement);
  if (!replacement_path.has_value()) return history_replace_outcome::Failed;
  defer { unused(os::remove_file(replacement_path->text().view())); };

  let const current_contents = path.read_entire_file();
  if (!current_contents.has_value() || current_contents->view() != original)
    return history_replace_outcome::Changed;
  if (!os::rename_path(replacement_path->text().view(), path.text().view()))
    return history_replace_outcome::Failed;

  let &state = get_no_editor_history_state();
  if (scan_no_editor_history(path, replacement) != history_scan_outcome::Loaded)
  {
    state.is_loaded = false;
    state.has_file_status = false;
  }

  return history_replace_outcome::Replaced;
}

template <class Match>
static fn find_no_editor_history_event(Allocator allocator,
                                       Maybe<usize> before_event_number,
                                       Match do_match)
    -> Maybe<toiletline::history_event>
{
  let const path = resolve_no_editor_history_path();
  if (!path.has_value()) return None;
  let contents = read_no_editor_history_contents(*path, false);
  if (contents.is_error()) return None;
  let &state = get_no_editor_history_state();
  let const retained_record_count = state.record_byte_offsets.count();
  let const first_number = state.total_count - retained_record_count + 1;
  let decoded = String{heap_allocator()};
  for (usize index = retained_record_count; index > 0; index--) {
    let const number = first_number + index - 1;
    if (before_event_number.has_value() && number >= *before_event_number)
      continue;
    let const span = get_history_record_span(state, index - 1);
    if (!decode_history_record(decoded, contents.value().view(), span))
      return None;
    if (do_match(number, decoded.view())) {
      return toiletline::history_event{
          number, String{allocator, decoded.view()}
      };
    }
  }

  return None;
}

static fn rewrite_no_editor_history_event(usize wanted_number,
                                          StringView expected,
                                          const ArrayList<String> &replacements)
    -> bool
{
  let const path = resolve_no_editor_history_path();
  if (!path.has_value()) return false;
  let const parent = path->parent_or_current();
  let lock = os::acquire_process_lock(parent.text().view());
  if (!lock.has_value()) return false;
  defer { os::release_process_lock(lock.take()); };
  let &state = get_no_editor_history_state();

  for (int attempt_index = 0;
       attempt_index < toiletline::HISTORY_RACE_ATTEMPT_COUNT; attempt_index++)
  {
    let const result = read_no_editor_history_contents(*path, false);
    if (result.is_error()) return false;

    let const &contents = result.value();
    let const retained_record_count = state.record_byte_offsets.count();
    if (retained_record_count == 0) return false;
    let const first_number = state.total_count - retained_record_count + 1;
    if (wanted_number < first_number ||
        wanted_number >= first_number + retained_record_count)
    {
      return false;
    }

    let const retained_index = wanted_number - first_number;
    let const span = get_history_record_span(state, retained_index);
    let decoded = String{heap_allocator()};
    if (!decode_history_record(decoded, contents.view(), span)) return false;
    if (decoded.view() != expected) return false;

    let rewritten = String{heap_allocator()};
    rewritten.reserve(contents.count());
    rewritten.append(contents.substring_of_length(0, span.start_byte_offset));
    for (let const &replacement : replacements) {
      if (replacement.count() > NO_EDITOR_HISTORY_ENTRY_MAX_BYTE_COUNT ||
          !toiletline::is_history_contents_valid(replacement.view()))
      {
        return false;
      }
      toiletline::encode_history_record(rewritten, replacement.view());
    }
    rewritten.append(contents.substring(span.end_byte_offset));

    let const outcome = replace_history_file(
        *path, contents.view(), rewritten.view(), ".kosh_history_fc");
    if (outcome == history_replace_outcome::Replaced) return true;
    if (outcome == history_replace_outcome::Failed) return false;

    /* A writer outside the lock moved the file. The snapshot the rewrite was
       built from is gone and the next attempt starts from fresh bytes. */
    state.is_loaded = false;
  }

  return false;
}

} /* namespace koshka::internal */

namespace toiletline {

using koshka::String;
using koshka::StringView;

fn enable_completion(koshka::EvalContext &context) -> void { unused(context); }

fn disable_completion() -> void {}

fn completion_is_enabled() -> bool { return false; }

fn enter_calc_history() -> void {}

fn leave_calc_history() -> void {}

fn get_history_path() -> koshka::Maybe<koshka::Path>
{
  return koshka::internal::resolve_no_editor_history_path();
}

/* Every event is appended to the file as it is stored. A write only has to
   drop the leading records the bounded list no longer reaches. */
fn history_write() -> koshka::ErrorOr<koshka::Ok>
{
  let const path = get_history_path();
  if (!path.has_value()) return koshka::Error{"the path is unavailable"};

  let const parent = path->parent_or_current();
  let lock = koshka::os::acquire_process_lock(parent.text().view());
  if (!lock.has_value())
    return koshka::Error{koshka::os::last_system_error_message()};
  defer { koshka::os::release_process_lock(lock.take()); };

  let &state = koshka::internal::get_no_editor_history_state();
  for (int attempt_index = 0;
       attempt_index < toiletline::HISTORY_RACE_ATTEMPT_COUNT; attempt_index++)
  {
    let contents =
        TRY(koshka::internal::read_no_editor_history_contents(*path, true));
    if (state.record_byte_offsets.is_empty()) return koshka::Success;

    let const first_byte_offset =
        koshka::internal::get_history_record_byte_offset(state, 0);
    if (first_byte_offset == 0) return koshka::Success;

    let const total_count = state.total_count;
    let const outcome = koshka::internal::replace_history_file(
        *path, contents.view(), contents.view().substring(first_byte_offset),
        ".kosh_history_write");
    if (outcome == koshka::internal::history_replace_outcome::Replaced) {
      state.total_count = total_count;
      return koshka::Success;
    }
    if (outcome == koshka::internal::history_replace_outcome::Failed)
      return koshka::Error{koshka::os::last_system_error_message()};

    /* A writer outside the lock moved the file. The offsets no longer describe
       it and the next attempt starts from fresh bytes. */
    state.is_loaded = false;
  }

  return koshka::Error{"the file kept changing"};
}

fn history_read() -> koshka::ErrorOr<koshka::Ok>
{
  let const path = get_history_path();
  if (!path.has_value()) return koshka::Error{"the path is unavailable"};

  return koshka::internal::load_no_editor_history(*path, false);
}

fn history_clear() -> koshka::ErrorOr<koshka::Ok>
{
  let const path = get_history_path();
  if (!path.has_value()) return koshka::Error{"the path is unavailable"};

  let const parent = path->parent_or_current();
  let lock = koshka::os::acquire_process_lock(parent.text().view());
  if (!lock.has_value())
    return koshka::Error{koshka::os::last_system_error_message()};
  defer { koshka::os::release_process_lock(lock.take()); };
  let opened = koshka::os::open_file_descriptor(
      path->text().view(), koshka::os::file_open_mode::Truncate);
  if (!opened.has_value())
    return koshka::Error{koshka::os::last_system_error_message()};
  if (!koshka::os::close_fd(opened.take()))
    return koshka::Error{koshka::os::last_system_error_message()};

  return koshka::internal::load_no_editor_history(*path, false);
}

fn set_history_enabled(bool is_enabled) -> void { unused(is_enabled); }

fn set_history_limit(usize entry_count) -> void
{
  let &state = koshka::internal::get_no_editor_history_state();
  let const retained_limit =
      entry_count < TL_HISTORY_MAX_SIZE ? entry_count : TL_HISTORY_MAX_SIZE;
  if (retained_limit == state.entry_limit) return;
  let const previous_limit = state.entry_limit;
  state.entry_limit = static_cast<u16>(retained_limit);
  if (retained_limit > previous_limit) {
    state.is_loaded = false;
    return;
  }
  let const retained_record_count = state.record_byte_offsets.count();
  if (retained_record_count <= retained_limit && state.first_record_index == 0)
  {
    return;
  }

  let retained_offsets = koshka::ArrayList<usize>{koshka::heap_allocator()};
  let const new_record_count = retained_record_count < retained_limit
                                   ? retained_record_count
                                   : retained_limit;
  retained_offsets.reserve(new_record_count);
  let const first_retained_index = retained_record_count - new_record_count;
  for (usize index = first_retained_index; index < retained_record_count;
       index++)
  {
    retained_offsets.push(
        koshka::internal::get_history_record_byte_offset(state, index));
  }

  state.record_byte_offsets = steal(retained_offsets);
  state.first_record_index = 0;
}

fn get_newest_history_event_number() -> koshka::Maybe<usize>
{
  let const path = get_history_path();
  if (!path.has_value()) return koshka::None;

  let const contents =
      koshka::internal::read_no_editor_history_contents(*path, false);
  if (contents.is_error()) return koshka::None;

  let const &state = koshka::internal::get_no_editor_history_state();
  if (state.record_byte_offsets.is_empty()) return koshka::None;

  return state.total_count;
}

fn get_history_events(koshka::Allocator allocator,
                      koshka::Maybe<usize> after_event_number)
    -> koshka::ErrorOr<koshka::ArrayList<history_event>>
{
  let events = koshka::ArrayList<history_event>{allocator};
  let const path = get_history_path();
  if (!path.has_value()) return steal(events);

  /* The read is allowed to miss the file. An absent history reads as an empty
     list and a damaged or unreadable file reads as a failure. */
  let const contents =
      TRY(koshka::internal::read_no_editor_history_contents(*path, true));
  let &state = koshka::internal::get_no_editor_history_state();
  let const retained_record_count = state.record_byte_offsets.count();
  let const first_number = state.total_count - retained_record_count + 1;
  usize first_index = 0;
  if (after_event_number.has_value() && *after_event_number >= first_number) {
    first_index = *after_event_number - first_number + 1;
  }

  events.reserve(retained_record_count - (first_index < retained_record_count
                                              ? first_index
                                              : retained_record_count));
  for (usize index = first_index; index < retained_record_count; index++) {
    let const span = koshka::internal::get_history_record_span(state, index);
    let command = String{allocator};
    if (!koshka::internal::decode_history_record(command, contents.view(),
                                                 span))
    {
      return koshka::Error{"the file contains invalid data"};
    }
    events.push(history_event{first_number + index, steal(command)});
  }

  return steal(events);
}

fn get_relative_history_event(koshka::Allocator allocator, usize distance,
                              koshka::Maybe<usize> before_event_number)
    -> koshka::Maybe<history_event>
{
  if (distance == 0) return koshka::None;
  usize remaining_event_count = distance;
  return koshka::internal::find_no_editor_history_event(
      allocator, before_event_number,
      [&](usize, StringView) { return --remaining_event_count == 0; });
}

fn get_numbered_history_event(koshka::Allocator allocator, usize number,
                              koshka::Maybe<usize> before_event_number)
    -> koshka::Maybe<history_event>
{
  return koshka::internal::find_no_editor_history_event(
      allocator, before_event_number, [&](usize candidate_number, StringView) {
        return candidate_number == number;
      });
}

fn get_prefixed_history_event(koshka::Allocator allocator, StringView prefix,
                              koshka::Maybe<usize> before_event_number)
    -> koshka::Maybe<history_event>
{
  return koshka::internal::find_no_editor_history_event(
      allocator, before_event_number,
      [&](usize, StringView command) { return command.starts_with(prefix); });
}

fn get_containing_history_event(koshka::Allocator allocator, StringView text,
                                koshka::Maybe<usize> before_event_number)
    -> koshka::Maybe<history_event>
{
  return koshka::internal::find_no_editor_history_event(
      allocator, before_event_number, [&](usize, StringView command) {
        return command.find_substring(text).has_value();
      });
}

fn history_append_event(StringView command) -> koshka::Maybe<usize>
{
  if (command.is_empty() ||
      command.length > koshka::internal::NO_EDITOR_HISTORY_ENTRY_MAX_BYTE_COUNT)
  {
    return koshka::None;
  }
  let const path = get_history_path();
  if (!path.has_value()) return koshka::None;
  let const parent = path->parent_or_current();
  let lock = koshka::os::acquire_process_lock(parent.text().view());
  if (!lock.has_value()) return koshka::None;
  defer { koshka::os::release_process_lock(lock.take()); };
  if (koshka::internal::ensure_no_editor_history_loaded(*path, true).is_error())
    return koshka::None;

  let &state = koshka::internal::get_no_editor_history_state();
  if (state.entry_limit == 0) return state.total_count;
  let const first_rune = koshka::utils::decode_utf8(command, 0, 0xfffd);
  if (first_rune.length >= command.length ||
      !toiletline::is_history_contents_valid(command))
  {
    return koshka::None;
  }
  if (!state.record_byte_offsets.is_empty()) {
    /* The load above proved the offsets describe the file. The duplicate check
       reads the newest record alone. */
    let const newest_span = koshka::internal::get_history_record_span(
        state, state.record_byte_offsets.count() - 1);
    let const encoded =
        koshka::internal::read_no_editor_history_span(*path, newest_span);
    if (!encoded.has_value()) {
      state.is_loaded = false;
      return koshka::None;
    }

    let newest = String{koshka::heap_allocator()};
    if (!koshka::internal::decode_history_record(newest, encoded->view(),
                                                 {0, encoded->count()}))
    {
      state.is_loaded = false;
      return koshka::None;
    }

    if (newest.view() == command) return state.total_count;
  }

  let const previous_file_byte_count = state.file_byte_count;
  let const had_unterminated_record =
      state.trailing_record_start_byte_offset != previous_file_byte_count;
  let payload = String{koshka::heap_allocator()};
  if (had_unterminated_record) payload.push('\n');
  encode_history_record(payload, command);
  let opened = koshka::os::open_file_descriptor(
      path->text().view(), koshka::os::file_open_mode::Append);
  if (!opened.has_value()) return koshka::None;
  let const fd = opened.value();
  let const was_written =
      koshka::os::write_all(fd, payload.data(), payload.count());
  let const was_closed = koshka::os::close_fd(fd);
  if (!was_written || !was_closed) {
    state.is_loaded = false;
    state.has_file_status = false;
    return koshka::None;
  }
  if (had_unterminated_record) {
    koshka::internal::push_history_record_byte_offset(
        state, state.trailing_record_start_byte_offset);
    state.total_count++;
  }

  let const record_start_byte_offset =
      previous_file_byte_count + (had_unterminated_record ? 1 : 0);
  koshka::internal::push_history_record_byte_offset(state,
                                                    record_start_byte_offset);
  state.total_count++;
  let const event_number = state.total_count;
  state.file_contents_hash = koshka::internal::extend_history_contents_hash(
      state.file_contents_hash, payload.view());
  state.file_byte_count = previous_file_byte_count + payload.count();
  state.trailing_record_start_byte_offset = state.file_byte_count;
  if (!koshka::internal::update_history_file_status(state, *path) ||
      state.file_status.size != state.file_byte_count)
  {
    state.is_loaded = false;
    state.has_file_status = false;
  }

  return event_number;
}

fn history_rewrite_event(usize number, StringView expected,
                         StringView replacement) -> bool
{
  let replacements = koshka::ArrayList<String>{koshka::heap_allocator()};
  if (!replacement.is_empty())
    replacements.push(String{koshka::heap_allocator(), replacement});
  return history_rewrite_event(number, expected, replacements);
}

fn history_rewrite_event(usize number, StringView expected,
                         const koshka::ArrayList<koshka::String> &replacements)
    -> bool
{
  return koshka::internal::rewrite_no_editor_history_event(number, expected,
                                                           replacements);
}

fn enable_job_notifications(koshka::EvalContext &context) -> void
{
  unused(context);
}

fn set_ghost_enabled(bool enabled) -> void { unused(enabled); }

fn set_highlight_enabled(bool enabled) -> void { unused(enabled); }

fn set_colors_enabled(bool enabled) -> void { unused(enabled); }

fn set_edit_mode(edit_mode mode) -> void { unused(mode); }

fn set_tab_selector(koshka::tab_selector_mode selector) -> void
{
  unused(selector);
}

fn is_active() -> bool { return false; }

fn initialize() -> void
{
  throw koshka::Error{
      "This build has no line editor, use '-c', '-s', or a file argument"};
}

fn exit(usize history_size_limit) -> void { unused(history_size_limit); }

fn set_title(StringView title) -> void { unused(title); }

fn set_idle_title() -> void {}

fn get_input(const String &prompt) -> input_result
{
  unused(prompt);
  throw koshka::Error{"This build has no line editor"};
}

fn set_input(const String &input) -> void { unused(input); }

fn enter_raw_mode() -> void {}

fn exit_raw_mode() -> void {}

fn emit_newlines(StringView buffer) -> void { unused(buffer); }

fn debug_allocation_failure() -> bool { return true; }

fn default_prompt_template() -> String
{
  let template_string = String{koshka::heap_allocator()};
  let const should_use_color = koshka::colors::stdout_wants_color();

  if (should_use_color) {
    template_string += "[\\u@\\h${KOSH_GIT_BRANCH:+ (";
    template_string += koshka::colors::ansi::CYAN;
    template_string += "$KOSH_GIT_BRANCH";
    template_string += koshka::colors::ansi::RESET;
    template_string += ")} ";
    template_string += koshka::colors::ansi::GREEN;
    template_string += "\\P";
    template_string += koshka::colors::ansi::RESET;
  } else {
    template_string += "[\\u@\\h${KOSH_GIT_BRANCH:+ ($KOSH_GIT_BRANCH)} \\P";
  }
  template_string += "] ";
  return template_string;
}

fn build_prompt(koshka::EvalContext &context) -> String
{
  unused(context);
  throw koshka::Error{"This build has no line editor"};
}

fn expand_prompt_template(StringView prompt, koshka::EvalContext &context)
    -> String
{
  unused(context);
  return String{prompt};
}

fn render_ps0(koshka::EvalContext &context) -> String
{
  unused(context);
  return String{koshka::heap_allocator()};
}

} /* namespace toiletline */

#endif /* KOSH_NO_TOILETLINE */
