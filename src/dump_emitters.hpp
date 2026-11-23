#pragma once

#include <cstdint>
#include <vector>

#include "json_writer.hpp"
#include "m2.hpp"

namespace husk::commands {

// Writes `ribbon_emitters`/`particle_emitters` -- unlike every other key
// `husk dump-chunks` writes, these come from the model's core MD20 header
// arrays (present in every version, including pre-Legion flat files), not a
// Legion+ chunk, so they're written unconditionally rather than gated
// behind `header.chunked` (see cmd_dump.cpp's top doc comment for how this
// relates to the bundle's placeholder emitter effects).
//
// model.ribbonEmitters/particleEmitters are already resolved by
// m2::loadModel. The particle version-gate check still reads model.header
// directly, never model.particleEmitters.empty() -- see the .cpp's call
// site for why.
void dumpEmitters(json::Writer& w, const m2::Model& model);

}  // namespace husk::commands
