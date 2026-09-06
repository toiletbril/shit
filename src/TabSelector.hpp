/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file defines the completion tab selector modes and their shared name
 * parser and printer. The mode reaches the runtime state, the command line, the
 * set builtin, and the interactive editor, so the names stay in one place.
 */

#pragma once

#include "Common.hpp"
#include "Maybe.hpp"
#include "StaticStringMap.hpp"
#include "StringView.hpp"

namespace koshka {

/* The presentation the editor uses when a completion has several candidates.
   Interactive draws the shell's own bounded menu under the prompt. External
   launches the configured selector program. Plain prints the candidate list the
   way a terminal shell without an editor does. */
enum class tab_selector_mode : u8
{
  Interactive,
  External,
  Plain,
};

inline pure fn parse_tab_selector_name(StringView name) throws
    -> Maybe<tab_selector_mode>
{
  static constexpr static_string_entry<tab_selector_mode>
      TAB_SELECTOR_ENTRIES[] = {
          {SSK("interactive"), tab_selector_mode::Interactive},
          {SSK("external"),    tab_selector_mode::External   },
          {SSK("plain"),       tab_selector_mode::Plain      },
  };
  static constexpr StaticStringMap TAB_SELECTORS{TAB_SELECTOR_ENTRIES};
  return TAB_SELECTORS.find(name);
}

inline pure fn tab_selector_name(tab_selector_mode selector) wontthrow
    -> StringView
{
  switch (selector) {
  case tab_selector_mode::Interactive: return "interactive";
  case tab_selector_mode::External: return "external";
  case tab_selector_mode::Plain: return "plain";
  }
  return "interactive";
}

} /* namespace koshka */
