#pragma once

#include <string>
#include <string_view>

namespace husk::text {

// A string from a game file as UTF-8. Names in WMO and M2 files are
// Windows-1252 where they leave ASCII ("Nefarian\\x92s Lair",
// blackrock2.wmo's MOGN), so text that isn't valid UTF-8 is decoded as
// Windows-1252; valid UTF-8 passes through unchanged.
std::string fromGameText(std::string_view bytes);

// Whether `bytes` is well-formed UTF-8.
bool isUtf8(std::string_view bytes);

}  // namespace husk::text
