#pragma once

#include "Common.hpp"
#include "Eval.hpp"

#include <string>

namespace shit {

static constexpr usize ERROR_CONTEXT_SIZE = 24;

struct ErrorBase
{
  ErrorBase();
  ErrorBase(const std::string &message);
  virtual ~ErrorBase();

  operator bool &();

  std::string message() const;

protected:
  bool        m_is_active{false};
  std::string m_message;
};

/**
 * Empty ctor creates an instance that converts to false, other ctors convert to
 * true.
 */
struct Error : public ErrorBase
{
  Error();
  Error(const std::string &message);

  std::string to_string();
};

/**
 * An error with location in the source code. The source must be supplied to
 * resolve context.
 */
struct ErrorWithLocation : public ErrorBase
{
  ErrorWithLocation();

  ErrorWithLocation(SourceLocation location, const std::string &message);

  virtual std::string to_string(std::string_view source);

protected:
  SourceLocation m_location;
};

struct ErrorWithLocationAndDetails : public ErrorWithLocation
{
  ErrorWithLocationAndDetails();

  ErrorWithLocationAndDetails(SourceLocation     location,
                              const std::string &message,
                              SourceLocation     details_location,
                              const std::string &details_message);

  std::string details_to_string(std::string_view source);

protected:
  SourceLocation m_details_location;
  std::string    m_details_message;
};

} /* namespace shit */
