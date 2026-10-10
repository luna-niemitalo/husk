#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// Per-element data with no dedicated canon field: a named channel over one
// of the mesh's element domains. Sources put facts here raw rather than
// growing a typed field per format detail.
namespace husk::canon {

enum class AttributeDomain {
    Vertex,  // one element per Mesh::positions entry
    Face,    // one element per Mesh::indices triangle, in index order
    Part,    // one element per Mesh::parts entry
};

using AttributeValues = std::variant<std::vector<uint8_t>, std::vector<uint16_t>, std::vector<uint32_t>,
                                     std::vector<int32_t>, std::vector<float>>;

struct AttributeChannel {
    std::string name;  // "<source>.<field>", unique within the mesh, e.g. "wmo.poly_flags"
    AttributeDomain domain = AttributeDomain::Vertex;
    uint32_t components = 1;
    AttributeValues values;  // element-major: `components` values per element
};

}  // namespace husk::canon
