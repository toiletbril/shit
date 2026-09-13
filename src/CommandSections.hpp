/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file classifies builtin and bundled utility kinds for command listings.
 * The shared classifier keeps the POSIX, Bash, and Koshka sections consistent
 * without duplicating the authoritative command name catalogs.
 */

#pragma once

#include "Builtin.hpp"
#include "Koshkit.hpp"

namespace koshka {

enum class command_section : u8
{
  Posix,
  Bash,
  Koshka,
};

inline pure fn get_builtin_section(Builtin::Kind kind,
                                   StringView name) wontthrow -> command_section
{
  if (name == "source") return command_section::Bash;

  switch (kind) {
  case Builtin::Kind::Assimilate:
  case Builtin::Kind::Bench:
  case Builtin::Kind::Koshkit:
  case Builtin::Kind::Z: return command_section::Koshka;
  case Builtin::Kind::Alias:
  case Builtin::Kind::Bg:
  case Builtin::Kind::Break:
  case Builtin::Kind::Cd:
  case Builtin::Kind::CommandBuiltin:
  case Builtin::Kind::Continue:
  case Builtin::Kind::Echo:
  case Builtin::Kind::Eval:
  case Builtin::Kind::Exec:
  case Builtin::Kind::Exit:
  case Builtin::Kind::Export:
  case Builtin::Kind::False:
  case Builtin::Kind::Fc:
  case Builtin::Kind::Fg:
  case Builtin::Kind::Getopts:
  case Builtin::Kind::Hash:
  case Builtin::Kind::Jobs:
  case Builtin::Kind::Kill:
  case Builtin::Kind::Newgrp:
  case Builtin::Kind::Printf:
  case Builtin::Kind::Pwd:
  case Builtin::Kind::Read:
  case Builtin::Kind::Readonly:
  case Builtin::Kind::Return:
  case Builtin::Kind::Set:
  case Builtin::Kind::Shift:
  case Builtin::Kind::Source:
  case Builtin::Kind::Test:
  case Builtin::Kind::Times:
  case Builtin::Kind::Trap:
  case Builtin::Kind::True:
  case Builtin::Kind::Type:
  case Builtin::Kind::Ulimit:
  case Builtin::Kind::Umask:
  case Builtin::Kind::Unalias:
  case Builtin::Kind::Unset:
  case Builtin::Kind::Wait: return command_section::Posix;
  default: return command_section::Bash;
  }
}

inline pure fn get_utility_section(koshkit::Utility::Kind kind) wontthrow
    -> command_section
{
  switch (kind) {
  case koshkit::Utility::Kind::Evil:
  case koshkit::Utility::Kind::EvilFiles:
  case koshkit::Utility::Kind::EvilFS:
  case koshkit::Utility::Kind::EvilNet:
  case koshkit::Utility::Kind::GoodNode:
  case koshkit::Utility::Kind::EvilPS:
  case koshkit::Utility::Kind::GoodStat:
  case koshkit::Utility::Kind::EvilDisk:
  case koshkit::Utility::Kind::EvilIO:
  case koshkit::Utility::Kind::EvilLogs:
  case koshkit::Utility::Kind::GoodCore:
  case koshkit::Utility::Kind::EvilSS:
  case koshkit::Utility::Kind::GoodFSW: return command_section::Koshka;
  default: return command_section::Posix;
  }
}

} /* namespace koshka */
