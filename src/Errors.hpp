#pragma once

#include "Common.hpp"
#include "Maybe.hpp"
#include "String.hpp"
#include "StringView.hpp"

namespace shit {

static constexpr usize ERROR_CONTEXT_SIZE = 24;

struct SourceLocation
{
  SourceLocation() = delete;
  SourceLocation(usize position, usize length);
  SourceLocation(usize position, usize length, Maybe<StringView> filename);

  /* Both variables are byte-offsets and do not account for unicode. */
  fn position() const -> usize;
  fn length() const -> usize;

  /* The name of the file the offset indexes, or None when the source has no
     name, such as an interactive line. A backtrace frame reads it to prefix the
     caret header with a path. */
  fn filename() const -> Maybe<StringView>;

  fn add_length(usize n) -> void;

private:
  usize m_position;
  usize m_length;
  Maybe<StringView> m_filename{};
};

struct ErrorBase
{
  ErrorBase();
  ErrorBase(StringView message);
  virtual ~ErrorBase();

  operator bool &();

  fn message() const -> String;

  /* The word printed before the message, Error by default. A warning subclass
     overrides i
     t to Warning, so the reporting code reads the severity from the
     object rather than taking it as an argument. */
  virtual fn severity_word() const -> String;

protected:
  bool m_is_active{false};
  String m_message;
};

struct Error : public ErrorBase
{
  Error();
  Error(StringView message);

  fn to_string() const -> String;

  /* Convert to the formatted message, so a call site passes an Error where a
     string is expected without spelling out to_string. */
  operator String() const;
};

/* An Error that prints as a warning and is shown rather than thrown. */
struct Warning : public Error
{
  Warning(StringView message);

  fn severity_word() const -> String override;
};

/* An Error that prints as a note and is shown rather than thrown. It carries no
   location, so it adds plain context under a primary error. */
struct Note : public Error
{
  Note(StringView message);

  fn severity_word() const -> String override;
};

/**
 * An error with location in the source code. The source must be supplied to
 * resolve context.
 */
struct ErrorWithLocation : public ErrorBase
{
  ErrorWithLocation();

  ErrorWithLocation(SourceLocation location, StringView message);

  /* The severity word comes from severity_word, so a warning subclass prints
     Warning over the same caret without passing the word in. */
  virtual fn to_string(StringView source) const -> String;

protected:
  SourceLocation m_location;
};

/* An ErrorWithLocation that prints as a warning and is shown rather than
   thrown. The prepass builds it to point a caret at a non-fatal issue. */
struct WarningWithLocation : public ErrorWithLocation
{
  WarningWithLocation(SourceLocation location, StringView message);

  fn severity_word() const -> String override;
};

struct ErrorWithLocationAndDetails : public ErrorWithLocation
{
  ErrorWithLocationAndDetails();

  ErrorWithLocationAndDetails(SourceLocation location, StringView message,
                              SourceLocation details_location,
                              StringView details_message);

  fn details_to_string(StringView source) const -> String;

protected:
  SourceLocation m_details_location;
  String m_details_message;
};

} /* namespace shit */
