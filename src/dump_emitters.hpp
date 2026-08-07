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
// behind `header.chunked`. Broadens this command's own stated scope (see
// cmd_dump.cpp's top doc comment): the "no glTF slot" rationale that already
// applied to the Legion+ side-chunks (TXAC/EXPT/RPID/GPID/PGD1) applies
// just as much to the parsed M2Ribbon/M2Particle records themselves --
// procedural emitter data, not renderable geometry -- while `husk export`
// still attaches a minimal position/bone anchor to the .glb's skin extras
// (see gltf::Skeleton::RibbonAnchor/ParticleAnchor) for placement without
// needing this JSON at all.
void dumpEmitters(json::Writer& w, const std::vector<uint8_t>& blob, const m2::Header& header);

}  // namespace husk::commands
