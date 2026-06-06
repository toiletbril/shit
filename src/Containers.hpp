#pragma once

/* The speed-oriented container and string types this shell uses in place of the
   standard library. Each type lives in its own header now, and this umbrella
   pulls them in together in dependency order, so a translation unit that wants
   the whole set keeps a single include. */

#include "ArrayList.hpp"
#include "HashMap.hpp"
#include "HashSet.hpp"
#include "PackedStringKey.hpp"
#include "StaticStringMap.hpp"
#include "String.hpp"
#include "StringView.hpp"
