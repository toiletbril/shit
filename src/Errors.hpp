#pragma once

#include "Common.hpp"
#include "Maybe.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace shit {

static constexpr usize ERROR_CONTEXT_SIZE = 24;

/* A byte range in some source, trivially copyable, so it is a plain struct
   passed and stored by value with no accessors. The position and length are
   byte offsets and do not account for unicode. The filename is the name of the
   file the offset indexes, or None when the source has no name, such as an
   interactive line, and a backtrace frame reads it to prefix the caret header
   with a path. */
struct SourceLocation
{
  usize position{0};
  usize length{0};
  Maybe<StringView> filename{};
};

/* Drop the cached source-line index that the located-error formatter keeps. The
   host calls this when it frees a retained source, so a later source allocated
   at the same address with the same length does not read a stale index. */
fn invalidate_source_line_index() wontthrow -> void;

class ErrorBase
{
public:
  ErrorBase();
  ErrorBase(StringView message);
  virtual ~ErrorBase();

  operator bool &() throws;

  fn message() const throws -> String;

  /* The word printed before the message, Error by default. A warning subclass
     overrides it to Warning, so the reporting code reads the severity from the
     object rather than taking it as an argument. */
  virtual fn severity_word() const wontthrow -> String;

  /* Renders the formatted message against the source the location indexes. The
     base render ignores the source since a plain error carries no location, and
     the located branch overrides it with the caret form, so a handler that
     caught any ErrorBase prints the right shape without RTTI. */
  virtual fn to_string(StringView source) const throws -> String;

  /* bash fails the current command and goes on after most evaluation errors,
     while a set -u read and a ${name:?} keep aborting the whole run. The flag
     marks the aborting kind, and the status is what the failed command
     reports, 1 for most errors and 2 for a [[ ]] operand error. A relocation
     that rewraps an error must carry both over. */
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

  /* A located error thrown from a function body is rendered at the call
     boundary while the call name stack still names the defining file, since the
     top-level handler renders against the typed line and cannot reach that
     file once the frame unwinds. The flag marks an error already shown there,
     so the top-level handler keeps the exit status but does not print it
     twice. */
  fn set_rendered() wontthrow -> void { m_was_rendered = true; }
  pure fn was_rendered() const wontthrow -> bool { return m_was_rendered; }

  /* The suggestion that accompanies the diagnostic, rendered as a trailing note
     line under the primary message rather than appended to it. The main message
     states the problem, and the note carries the advice, so a reader sees the
     fix on its own line. A relocation that rewraps an error carries it over. */
  fn set_note(StringView note) throws -> void { m_note = note; }
  pure fn has_note() const wontthrow -> bool { return !m_note.is_empty(); }
  fn note() const throws -> String { return m_note; }

protected:
  /* The "note: <suggestion>." trailing line in the note severity hue, or an
     empty string when no note is attached, so a caller appends it
     unconditionally and emits nothing on the no-note path. */
  fn note_to_string() const throws -> String;

  bool m_is_active{false};
  bool m_is_script_fatal{false};
  bool m_was_rendered{false};
  i64 m_command_status{1};
  String m_message;
  String m_note;
};

class Error : public ErrorBase
{
public:
  Error();
  Error(StringView message);

  fn to_string() const throws -> String;
  using ErrorBase::to_string;

  /* Convert to the formatted message, so a call site passes an Error where a
     string is expected without spelling out to_string. */
  operator String() const throws;
};

/* An Error that prints as a warning and is shown rather than thrown. */
class Warning : public Error
{
public:
  Warning(StringView message);

  fn severity_word() const wontthrow -> String override;
};

/* An Error that prints as a note and is shown rather than thrown. It carries no
   location, so it adds plain context under a primary error. */
class Note : public Error
{
public:
  Note(StringView message);

  fn severity_word() const wontthrow -> String override;
};

/* Thrown when an exec fails with ENOEXEC, where the file is executable but is
   not a valid binary and carries no shebang. It signals the runtime to run the
   file as a shell script, the POSIX fallback, rather than reporting a failure,
   so it is always caught and never shown. */
class ExecFormatError : public Error
{
public:
  ExecFormatError();
};

/**
 * An error with location in the source code. The source must be supplied to
 * resolve context.
 */
class ErrorWithLocation : public ErrorBase
{
public:
  ErrorWithLocation();

  ErrorWithLocation(SourceLocation location, StringView message);

  /* The severity word comes from severity_word, so a warning subclass prints
     Warning over the same caret without passing the word in. */
  virtual fn to_string(StringView source) const throws -> String;

  /* The line numbering starts this many lines past one, for a source that is
     a window into a larger file, the stored function definition text whose
     first line sits deep inside the defining file. */
  fn set_line_offset(usize offset) wontthrow -> void { m_line_offset = offset; }

  /* The stored location, read at the call boundary so a function-body error
     rebases its absolute position onto the definition copy before it renders.
   */
  pure fn location() const wontthrow -> SourceLocation { return m_location; }
  fn set_location(SourceLocation location) wontthrow -> void
  {
    m_location = location;
  }

protected:
  SourceLocation m_location;
  usize m_line_offset{0};
};

/* An ErrorWithLocation raised when a command word resolves to neither a
   builtin, a program on PATH, nor an existing path at runtime. The simple
   command boundary catches it, prints it to stderr, and yields exit status 127
   so evaluation continues the way a normal failing command does, rather than
   aborting the shell. */
class CommandNotFound : public ErrorWithLocation
{
public:
  CommandNotFound(SourceLocation location, StringView message);
};

/* An ErrorWithLocation that prints as a warning and is shown rather than
   thrown. The prepass builds it to point a caret at a non-fatal issue. */
class WarningWithLocation : public ErrorWithLocation
{
public:
  WarningWithLocation(SourceLocation location, StringView message);

  fn severity_word() const wontthrow -> String override;
};

/* An ErrorWithLocation that prints as a trace and is shown rather than thrown.
   A source backtrace frame builds it so a call site reads as context under the
   primary error rather than as an error of its own. */
class TraceWithLocation : public ErrorWithLocation
{
public:
  TraceWithLocation(SourceLocation location);

  fn severity_word() const wontthrow -> String override;
};

/* Rewraps a plain error at a source location, carrying the script-fatal mark
   and the command status over, so a relocated set -u read still aborts and a
   relocated [[ ]] operand error still reports its status. */
inline fn relocate_error(const Error &error, SourceLocation location) throws
    -> ErrorWithLocation
{
  let relocated = ErrorWithLocation{location, error.message().view()};
  if (error.is_script_fatal()) relocated.set_script_fatal();
  relocated.set_command_status(error.command_status());
  if (error.has_note()) relocated.set_note(error.note().view());
  return relocated;
}

class ErrorWithLocationAndDetails : public ErrorWithLocation
{
public:
  ErrorWithLocationAndDetails();

  ErrorWithLocationAndDetails(SourceLocation location, StringView message,
                              SourceLocation details_location,
                              StringView details_message);

  fn details_to_string(StringView source) const throws -> String;

protected:
  SourceLocation m_details_location;
  String m_details_message;
};

} /* namespace shit */
