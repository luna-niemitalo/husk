#pragma once

// M2 header parsing, per https://wowdev.wiki/M2 (fetched 2026-07-24).
//
// Two on-disk shapes:
//  - Pre-Legion: the file *is* the MD20 header + data, starting with magic
//    "MD20" at byte 0.
//  - Legion+ (expansion level >= 7, build >= 7.0.1.20740): the file is
//    husk::readChunks()-style chunks in arbitrary order; the MD21 chunk's
//    payload is byte-for-byte the old MD20 blob, with every offset in it
//    relative to the *chunk's* start, not the file's.
//
// Either way, once you have the MD20 blob, the header layout is identical.
// This module resolves the outer shape first, then parses that one blob.
//
// This is the index/aggregate header -- was a single 1175-line file until
// FILE_SPLIT_TODO.md Item 2 split it by concern into the five headers
// below, each reopening namespace husk::m2 directly, so every existing
// `#include "m2.hpp"` caller (src/, tests/) keeps working unchanged:
//  - m2_primitives.hpp: Array/Vec3/Vec2/Quat/BoundingBox/ParseError, the
//    bounds-checked blob-read helpers, and the top-level parseHeader/
//    loadFile/extractBlob/expansionForVersion entry points.
//  - m2_header.hpp: Header/Texture/Material, GlobalFlag/BoneFlag.
//  - m2_skeleton.hpp: Bone/Vertex/CollisionMesh.
//  - m2_animation.hpp: Sequence and every M2Track/FBlock/M2PartTrack
//    curve resolver (bone + material + particle tracks alike).
//  - m2_scene.hpp: Attachment/Event/Light/Ribbon/ParticleEmitter/
//    ExtendedParticle.
#include "m2_animation.hpp"
#include "m2_header.hpp"
#include "m2_primitives.hpp"
#include "m2_scene.hpp"
#include "m2_skeleton.hpp"
