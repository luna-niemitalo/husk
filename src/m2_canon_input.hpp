#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "canon_model.hpp"
#include "m2.hpp"
#include "m2_material_input.hpp"  // TextureResolutions
#include "skel.hpp"
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

// A .skel-sourced bones/sequences/track-blob triple, replacing the model's
// own inline `model.bones`/`model.sequences`/`model.blob` for skeleton and
// bone-animation assembly ONLY -- mesh geometry (`model.vertices`) and
// every material-related table (colors/textureWeights/textureTransforms,
// keyed against `model.blob`) are never .skel-sourced, per wowdev.wiki
// M2/.skel's own scope ("These files replace SOME blocks from the M2 MD20
// data"), so `buildCanonModel` keeps reading those from `model` unchanged
// regardless of whether this parameter is supplied.
//
// Mirrors `commands::resolveBones`/`resolveAnimationsForModel`'s own
// `haveSkel` branch (`cmd_export.cpp`) at the type level: `bones`/
// `sequences`/`blob` are exactly `skel::parseBones(skelBytes)`/
// `skel::parseSequences(skelBytes)`/`skel::boneTrackBlob(skelBytes)`'s own
// return shapes. Parsing the .skel file and deciding whether one applies
// at all (an M2 with 0 inline bones, `--skel` not `none`, a real `.skel`
// file found) is the caller's job -- this module has no filesystem access
// (I1) and no opinion on which source an M2 should use.
struct ExternalSkeletonSource {
    std::vector<m2::Bone> bones;
    std::vector<m2::Sequence> sequences;
    std::vector<uint8_t> blob;
    std::optional<skel::Attachments> attachments;  // skel::findAttachments; replaces model.attachments when set
};

// Builds a canon::Model from one real M2 model + its already-resolved
// skin tier, mirroring the legacy pipeline's own three real animation
// shapes (real-inline, global-sequence, alias) and deduping materials by
// the same real M2 identity every distinct material actually has: a
// batch's own (materialIndex, textureCount, textureComboIndex,
// textureCoordComboIndex, colorIndex, textureWeightComboIndex,
// textureTransformComboIndex) tuple fully determines what
// m2input::assembleMaterial would build for it (given the fixed
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
// `externalSkeleton` defaults to nullptr, meaning "use model.bones/
// model.sequences/model.blob" -- exactly today's behavior, byte-for-byte
// unchanged for every existing caller. When supplied, skeleton assembly
// and every bone-animation pass (real-inline, global-sequence, alias) use
// `externalSkeleton->bones`/`sequences`/`blob` instead -- mesh skinning's
// own bone-count bound follows the same source, so a .skel-sourced model's
// vertex bone indices are checked against the RIGHT bone count, not the
// model's own (usually empty) inline one.
//
// `firstResolvedSequenceIndex` (the index this function threads into
// `assembleMaterial` as `sequenceIndex`) is always an index into whichever
// `sequences` array actually produced a clip -- `externalSkeleton->sequences`
// when supplied. Material animation tracks (M2Color/M2TextureWeight/
// M2TextureTransform) stay inline-M2-sourced regardless (`model.blob`), so
// this is a real, narrow scope boundary: a .skel-sourced model's material
// tint/alpha-fade/UV-animation curves are resolved against a sequence INDEX
// chosen from the .skel's own sequence count, on the documented assumption
// (wowdev.wiki, corroborated by `skel.hpp`'s own doc comment on SKB1 bone
// tracks sharing SKS1's sequence-array position 1:1) that inline material
// M2Track arrays are sized to match whichever sequence source is actually
// in effect for the model, not a separate hard-coded assumption of this
// function's own invention -- not independently verified against real
// data for the material-track case specifically, named here rather than
// silently assumed correct.
//
// The model's scene (attachments, events, lights, emitters) comes from
// assembleScene (m2_scene_input.hpp). Its external-.anim keyframes are used
// only for a model's own sequences: a .skel-sourced model's external blobs
// hold skeleton data, so its scene tracks resolve inline only.
//
// Throws std::runtime_error wherever assembleSkeleton/assembleMesh/
// assembleMaterial/assembleBoneAnimation/assembleScene/canon::assembleModel
// already would -- no new corruption checks are added here.
canon::Model buildCanonModel(const m2::Model& model, const std::vector<skin::Batch>& batches,
                              const std::vector<skin::Submesh>& submeshes,
                              const std::vector<uint32_t>& triangleIndices,
                              const ExternalAnimBlobs& externalAnimBlobs = {},
                              const TextureResolutions& textureResolutions = {},
                              const ExternalSkeletonSource* externalSkeleton = nullptr);

}  // namespace husk::m2input
