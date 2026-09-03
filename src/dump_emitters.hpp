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
//
// Takes the whole `m2::Model` (REFACTOR/AUDIT.md §2.1's migration --
// cmd_dump.cpp used to call m2::parseRibbons/parseParticles itself and pass
// just blob+header) rather than re-parsing: model.ribbonEmitters/
// particleEmitters are already the same parseRibbons/parseParticles output,
// resolved once by m2::loadModel. The version-gate check below still reads
// model.header directly (never model.ribbonEmitters/particleEmitters
// themselves) -- see cmd_dump.cpp's call site for why that guard has to
// survive unchanged even though the parse itself moved.
void dumpEmitters(json::Writer& w, const m2::Model& model);

}  // namespace husk::commands
