#pragma once

#include "Common.hpp"
#include "Debug.hpp"

#include <iostream>
#include <string>
#include <tuple>

namespace shit {

static constexpr usize ERROR_CONTEXT_SIZE = 24;

struct ErrorBase
{
  ErrorBase();
  ErrorBase(std::string message);
  virtual ~ErrorBase();

  operator bool &();

  std::string message() const;

protected:
  bool        m_is_active{false};
  std::string m_message;
};

/**
 * A standard error. Made to be used without dynamic allocations: note that it
 * has bool conversion defined, which is supposed to tell whether is error is an
 * actual one, or just an empty sentinel.
 *
 * Empty ctor creates an instance that converts to false, other ctors convert to
 * true.
 */
struct Error : public ErrorBase
{
  Error();
  Error(std::string message);

  std::string to_string();
};

/**
 * An error with location in the source code. The source must be supplied to
 * resolve context.
 */
struct ErrorWithLocation : public ErrorBase
{
  ErrorWithLocation();

  ErrorWithLocation(usize location, std::string message);

  virtual std::string to_string(std::string_view source);

protected:
  usize m_location{0};
};

struct ErrorWithLocationAndDetails : public ErrorWithLocation
{
  ErrorWithLocationAndDetails();

  ErrorWithLocationAndDetails(usize location, std::string message,
                              usize       details_location,
                              std::string details_message);

  std::string details_to_string(std::string_view source);

protected:
  usize       m_details_location{0};
  std::string m_details_message;
};

} /* namespace shit */
