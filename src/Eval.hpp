#pragma once

#include "Common.hpp"

#include <string>
#include <vector>

namespace shit {

struct SourceLocation
{
  SourceLocation() = delete;
  SourceLocation(usize position, usize length);

  usize position() const;
  usize length() const;

private:
  usize m_position;
  usize m_length;
};

struct EscapeMap
{
  EscapeMap();

  void add_escape(usize position);
  bool is_escaped(usize position) const;

private:
  std::vector<u8> m_bitmap;
};

struct Token;

struct EvalContext
{
  EvalContext(bool should_disable_path_expansion);

  void add_expansion();
  void add_evaluated_expression();

  void end_command();

  void steal_escape_map(const EscapeMap &&em);

  /* Path-expand, tilde-expand and escape. */
  std::vector<std::string> process_args(const std::vector<const Token *> &args);

  std::string make_stats_string() const;

  usize last_expressions_executed() const;
  usize total_expressions_executed() const;

  usize last_expansion_count() const;
  usize total_expansion_count() const;

protected:
  bool m_enable_path_expansion;

  EscapeMap m_escape_map;

  std::vector<std::string> expand_path_once(std::string_view r,
                                            bool should_count_files);
  std::vector<std::string>
  expand_path_recurse(const std::vector<std::string> &vs);
  std::vector<std::string> expand_path(std::string &&r);

  void expand_tilde(std::string &r);
  void erase_escapes(std::string &r);

  usize m_expressions_executed_last{0};
  usize m_expressions_executed_total{0};
  usize m_expansions_last{0};
  usize m_expansions_total{0};
};

} /* namespace shit */
