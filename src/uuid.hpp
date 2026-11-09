#pragma once

#include <string>
#include <string_view>

namespace husk {

// RFC 4122 version 3 (MD5, name-based) UUID of `name` in husk's own
// namespace, lowercase canonical text. The same name always gives the same
// id, so documents stay deterministic and an id survives a re-export.
// Python's uuid.uuid3(HUSK_UUID_NAMESPACE, name) gives the same value; the
// namespace is duplicated in blender/addons/husk_addon/zones.py.
std::string nameUuid(std::string_view name);

}  // namespace husk
