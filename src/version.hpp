#pragma once

namespace husk {

// The build version (CMakeLists.txt's HUSK_VERSION). Defined in its own
// translation unit so a version change rebuilds one file, not the library.
const char* version();

}  // namespace husk
