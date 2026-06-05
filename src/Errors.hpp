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

  /* Both variables are byte-offsets and do not account for unicode. */
  usize position() const;
  usize length() const;

  void add_length(usize n);

private:
  usize m_position;
  usize m_length;
};

struct ErrorBase
{
  ErrorBase();
  ErrorBase(StringView message);
  virtual ~ErrorBase();

  operator bool &();

  String message() const;

  /* The word printed before the message, Error by default. A warning subclass
     overrides i
     t to Warning, so the reporting code reads the severity from the
     object rather than taking it as an argument. */
  virtual String severity_word() const;

protected:
  bool m_is_active{false};
  String m_message;
};

struct Error : public ErrorBase
{
  Error();
  Error(StringView message);

  String to_string() const;

  /* Convert to the formatted message, so a call site passes an Error where a
     string is expected without spelling out to_string. */
  operator String() const;
};

/* An Error that prints as a warning and is shown rather than thrown. */
struct Warning : public Error
{
  Warning(StringView message);

  String severity_word() const override;
};

/* An Error that prints as a note and is shown rather than thrown. It carries no
   location, so it adds plain context under a primary error. */
struct Note : public Error
{
  Note(StringView message);

  String severity_word() const override;
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
  virtual String to_string(StringView source) const;

protected:
  SourceLocation m_location;
};

/* An ErrorWithLocation that prints as a warning and is shown rather than
   thrown. The prepass builds it to point a caret at a non-fatal issue. */
struct WarningWithLocation : public ErrorWithLocation
{
  WarningWithLocation(SourceLocation location, StringView message);

  String severity_word() const override;
};

struct ErrorWithLocationAndDetails : public ErrorWithLocation
{
  ErrorWithLocationAndDetails();

  ErrorWithLocationAndDetails(SourceLocation location, StringView message,
                              SourceLocation details_location,
                              StringView details_message);

  String details_to_string(StringView source) const;

protected:
  SourceLocation m_details_location;
  String m_details_message;
};

} /* namespace shit */
