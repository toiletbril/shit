#pragma once

#include "Common.hpp"
#include "Maybe.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace shit {

static constexpr usize ERROR_CONTEXT_SIZE = 24;

struct SourceLocation
{
  usize position{0};
  usize length{0};
  Maybe<StringView> filename{};
};

/* Called when a retained source is freed, so a later source allocated at the
   same address with the same length does not read a stale index. */
fn invalidate_source_line_index() wontthrow -> void;

class ErrorBase
{
public:
  ErrorBase();
  ErrorBase(StringView message);
  virtual ~ErrorBase();

  operator bool &() throws;

  fn message() const throws -> String;

  virtual fn severity_word() const wontthrow -> String;

  virtual fn to_string(StringView source) const throws -> String;

  /* The command status is 1 for most errors and 2 for a [[ ]] operand error. A
     relocation that rewraps an error must carry both the fatal mark and the
     status over. */
  fn set_script_fatal() wontthrow -> void { m_is_script_fatal = true; }
  pure fn is_script_fatal() const wontthrow -> bool
  {
    return m_is_script_fatal;
  }
  fn set_command_status(i64 status) wontthrow -> void
  {
    m_command_status = status;
  }
  pure fn command_status() const wontthrow -> i64 { return m_command_status; }

  fn set_rendered() wontthrow -> void { m_was_rendered = true; }
  pure fn was_rendered() const wontthrow -> bool { return m_was_rendered; }

  pure fn has_note() const wontthrow -> bool { return !m_note.is_empty(); }
  fn note() const throws -> String { return m_note; }

protected:
  fn note_to_string() const throws -> String;

  bool m_is_active{false};
  bool m_is_script_fatal{false};
  bool m_was_rendered{false};
  i64 m_command_status{1};
  String m_message{heap_allocator()};
  String m_note{heap_allocator()};
};

class Error : public ErrorBase
{
public:
  Error();
  Error(StringView message);

  fn to_string() const throws -> String;
  using ErrorBase::to_string;

  operator String() const throws;
};

/* An Error paired with a note, the trailing hint line under the message. The
   note is set once at construction, the ordinary Error carries none. */
class ErrorWithDetails : public Error
{
public:
  ErrorWithDetails(StringView message, StringView note);
};

class Warning : public Error
{
public:
  Warning(StringView message);

  fn severity_word() const wontthrow -> String override;
};

class WarningWithDetails : public Warning
{
public:
  WarningWithDetails(StringView message, StringView note);
};

/* The mimic boundary tests this type, never the message text, so a
   program-thrown Error reading "Interrupted" is not mistaken for it. */
class InterruptError : public Error
{
public:
  InterruptError();
};

class Note : public Error
{
public:
  Note(StringView message);

  fn severity_word() const wontthrow -> String override;
};

/* Thrown when an exec fails with ENOEXEC, so the runtime runs the file as a
   shell script, the POSIX fallback. It is always caught and never shown. */
class ExecFormatError : public Error
{
public:
  ExecFormatError();
};

class ErrorWithLocation : public ErrorBase
{
public:
  ErrorWithLocation();

  ErrorWithLocation(SourceLocation location, StringView message);

  virtual fn to_string(StringView source) const throws -> String;

  /* The line numbering starts this many lines past one, for a source that is a
     window into a larger file. */
  fn set_line_offset(usize offset) wontthrow -> void { m_line_offset = offset; }

  pure fn location() const wontthrow -> SourceLocation { return m_location; }
  fn set_location(SourceLocation location) wontthrow -> void
  {
    m_location = location;
  }

protected:
  SourceLocation m_location;
  usize m_line_offset{0};
};

/* The simple command boundary catches it, prints it, and yields status 127 so
   evaluation continues instead of aborting the shell. */
class CommandNotFound : public ErrorWithLocation
{
public:
  CommandNotFound(SourceLocation location, StringView message);
  CommandNotFound(SourceLocation location, StringView message, StringView note);
};

class WarningWithLocation : public ErrorWithLocation
{
public:
  WarningWithLocation(SourceLocation location, StringView message);

  fn severity_word() const wontthrow -> String override;
};

class WarningWithLocationAndDetails : public WarningWithLocation
{
public:
  WarningWithLocationAndDetails(SourceLocation location, StringView message,
                                StringView note);
};

class TraceWithLocation : public ErrorWithLocation
{
public:
  TraceWithLocation(SourceLocation location);

  fn severity_word() const wontthrow -> String override;
};

class ErrorWithLocationAndDetails : public ErrorWithLocation
{
public:
  ErrorWithLocationAndDetails();

  ErrorWithLocationAndDetails(SourceLocation location, StringView message,
                              SourceLocation details_location,
                              StringView details_message);

  /* A located error carrying a note, the trailing hint line, without a second
     caret. */
  ErrorWithLocationAndDetails(SourceLocation location, StringView message,
                              StringView note);

  fn details_to_string(StringView source) const throws -> String;

protected:
  SourceLocation m_details_location;
  String m_details_message;
};

/* The relocation rewraps an unlocated error onto a span and rethrows it,
   carrying the fatal mark, the status, and the note over. A noted error becomes
   the located-and-details form so the note survives. */
[[noreturn]] inline fn relocate_error(const ErrorBase &error,
                                      SourceLocation location) throws -> void
{
  if (error.has_note()) {
    let relocated = ErrorWithLocationAndDetails{
        location, error.message().view(), error.note().view()};
    if (error.is_script_fatal()) relocated.set_script_fatal();
    relocated.set_command_status(error.command_status());
    throw relocated;
  }

  let relocated = ErrorWithLocation{location, error.message().view()};
  if (error.is_script_fatal()) relocated.set_script_fatal();
  relocated.set_command_status(error.command_status());
  throw relocated;
}

} /* namespace shit */
