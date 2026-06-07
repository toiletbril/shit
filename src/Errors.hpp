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
     overrides i
     t to Warning, so the reporting code reads the severity from the
     object rather than taking it as an argument. */
  virtual fn severity_word() const wontthrow -> String;

protected:
  bool m_is_active{false};
  String m_message;
};

class Error : public ErrorBase
{
public:
  Error();
  Error(StringView message);

  fn to_string() const throws -> String;

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

protected:
  SourceLocation m_location;
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
