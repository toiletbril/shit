#pragma once

#include "Common.hpp"
#include "MimicMood.hpp"

namespace shit {

class EvalContext;

/* A snapshot of the mood and the diagnostic and strictness toggles, captured
   and restored as a unit so a scope that runs a body under a different mood
   saves and puts back the whole set with one call. */
class RuntimeState
{
public:
  mimic_mood mood{mimic_mood::Default};
  bool are_warnings_enabled{false};
  bool are_diagnostics_disabled{false};
  bool error_unset{false};
  bool pipefail{false};
  bool failglob{false};
  /* The explicit marks ride with their values, so a set -u inside a
     mood-swapped scope does not leave the mark set after the value reverts. */
  bool error_unset_explicit{false};
  bool pipefail_explicit{false};
  bool failglob_explicit{false};

  mustuse static fn capture(const EvalContext &context) wontthrow
      -> RuntimeState;
  fn restore(EvalContext &context) const wontthrow -> void;
};

} // namespace shit
