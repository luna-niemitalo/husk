#pragma once

#include "json_writer.hpp"
#include "phys.hpp"

namespace husk::commands {

// Full `.phys` dump -- every body/shape/joint/PHYV record, each shape/joint
// resolved to its real type-specific data inline (see cmd_dump.cpp's own top
// doc comment, DESIGN.md's Key design decisions).
void writePhysFile(json::Writer& w, const phys::File& f);

}  // namespace husk::commands
