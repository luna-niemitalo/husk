#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "canon_material_builder.hpp"  // TextureResolutions
#include "canon_model.hpp"
#include "m2.hpp"
#include "skin.hpp"

// husk::m2input: the M2 input module -- everything that decides *how* one
// real M2 model's bytes become a canon::Model, as opposed to canon_model.hpp
// itself, which only knows how to compose already-built canon:: pieces
// (see that file's own doc comment for the split and why it exists).
//
// This is the ONE place that walks `skin::Batch`/`m2::Sequence` to decide
// which sequences produce a clip, which batches share a material, and how
// many distinct canon::Material objects a model actually needs -- the
// orchestration `canon::assembleModel` itself used to do before this split.
// It calls the same assembleMesh/assembleSkeleton/assembleMaterial/
// assembleBoneAnimation(Global) functions (canon_*_builder.hpp) that
// existed before this split; only the orchestration moved, not the
// per-piece translation logic those functions already own.
namespace husk::m2input {

// One real M2Sequence index's already-loaded external .anim payload bytes
// -- see canon_model.hpp's OLD doc comment history for the full contract
// this used to state there; moved here since "which sequence index needs
// which external file" is M2-specific resolution-unit bookkeeping, not a
// canon:: concept. Resolution -- deciding which FileDataID's bytes to
// fetch, finding the file on disk, and reading it -- happens entirely
// outside both this file and canon:: (I1); `buildCanonModel` only consumes
// already-resolved bytes handed to it by its caller. Keyed by the model's
// own `m2::Model::sequences` index space. Empty by default: a sequence with
// no entry here that also isn't real-inline or a resolvable alias to a
// real-inline/external terminal is simply absent from the resulting
// canon::Model::animations, exactly today's behavior.
using ExternalAnimBlobs = std::unordered_map<uint32_t, std::vector<uint8_t>>;

// Builds a canon::Model from one real M2 model + its already-resolved
// skin tier, mirroring the legacy pipeline's own three real animation
// shapes (real-inline, global-sequence, alias) and deduping materials by
// the same real M2 identity every distinct material actually has: a
// batch's own (materialIndex, textureCount, textureComboIndex,
// textureCoordComboIndex, colorIndex, textureWeightComboIndex,
// textureTransformComboIndex) tuple fully determines what
// canon::assembleMaterial would build for it (given the fixed
// `textureResolutions`/sequence-index this function already threads
// through uniformly) -- so two batches sharing that tuple share one
// canon::Material, found by identity rather than by hashing the built
// material's own content after the fact (legacy's own materialDedupKey,
// export_texture_resolution.cpp, does the latter -- more expensive, and a
// real float-stringification fragility risk this tuple-identity approach
// doesn't have).
//
// `externalAnimBlobs`/`textureResolutions` default to empty, preserving
// every existing caller's exact current behavior (no external-.anim
// sequence resolves, every texture stays KnownUnresolved) when the caller
// has nothing to offer -- see each type's own doc comment.
//
// Throws std::runtime_error wherever assembleSkeleton/assembleMesh/
// assembleMaterial/assembleBoneAnimation/canon::assembleModel already
// would -- no new corruption checks are added here.
canon::Model buildCanonModel(const m2::Model& model, const std::vector<skin::Batch>& batches,
                              const std::vector<skin::Submesh>& submeshes,
                              const std::vector<uint32_t>& triangleIndices,
                              const ExternalAnimBlobs& externalAnimBlobs = {},
                              const canon::TextureResolutions& textureResolutions = {});

}  // namespace husk::m2input
