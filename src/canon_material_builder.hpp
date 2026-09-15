#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "canon_material.hpp"
#include "m2_animation.hpp"  // m2::Color/TextureWeight/TextureTransform
#include "m2_header.hpp"     // m2::Material/Texture
#include "skin.hpp"          // skin::Batch

// husk::canon: assembly of one canon::Material (and its canon::MaterialLayer
// stack) from a real .skin Batch plus the M2's own material/texture/color/
// transform tables -- the adjacent twin of buildMaterialsAndPrimitives's
// per-batch material construction (export_materials.cpp), built to prove
// structural convergence (REFACTOR/README.md stage 3's gate) without
// touching the existing pipeline.
//
// Does NOT itself PERFORM texture resolution: DB2/listfile/
// sources::Catalog lookups are entirely the input/parsing-and-solving
// layer's job (REFACTOR/AUDIT.md's "no paths in canon::" invariant, I1) --
// canon:: never touches a filesystem or a Catalog. It CAN, however, RECORD
// a resolution a caller already performed and hands in via
// `assembleMaterial`'s optional `textureResolutions` parameter (see below):
// when a layer's key is present in that map, its TextureRef is copied
// through verbatim (Resolved/KnownUnresolved/Ambiguous, whichever the
// caller already decided); when absent -- including the default, empty map
// every existing caller still gets -- the layer falls back to the original
// unconditional TextureRef::State::KnownUnresolved, unchanged from before
// this parameter existed. Also does NOT build ShadingFunction/Function:
// decoding a resolved shaderId's real Combiners_* name into a structured
// per-stage op chain is exactly the "porting real Combiners_* formula math
// into ShadingFunction::function" work canon_material.hpp's own header
// comment already defers to separate, later work -- guessing at it here
// would be new invented vocabulary, not a mirrored fact.
namespace husk::canon {

// One already-resolved (or explicitly failed/ambiguous) texture lookup,
// resolved entirely outside canon:: by the caller -- see this header's own
// top comment and REFACTOR/AUDIT.md's §7.1. Keyed by the same M2 texture
// array index `MaterialLayer::identity` already uses as its own
// `RecordIndex` (canon_material_builder.cpp's `layerIdentity`/
// `assemblePrimaryLayer`'s `textureIndex` -- i.e. the resolved index into
// `M2MaterialInputs::textures`, NOT a raw `skin::Batch`/`textureCombos`
// position) -- reusing that existing identity key instead of inventing a
// second one for the same texture unit.
using TextureResolutions = std::unordered_map<uint32_t, TextureRef>;

// Everything assembleMaterial needs out of the M2 itself, mirroring
// commands::M2MaterialInputs (export_texture_resolution.hpp)'s own
// bundling reasoning (one call site, many arrays, no real abstraction cost)
// -- kept as a separate type here, not reused directly, since that struct
// lives in husk::commands and pulls in gltf.hpp; canon:: stays free of any
// writer dependency, the same boundary every other canon_*_builder.hpp
// respects (e.g. canon_mesh_builder.hpp takes plain skin:: types, not
// gltf::Primitive).
struct M2MaterialInputs {
    std::vector<m2::Material> materials;
    std::vector<m2::Texture> textures;
    std::vector<uint16_t> textureCombos;
    // Pre-Cataclysm only (wowdev.wiki M2/.skin#geosetIndex: "Still present
    // but unused in Cataclysm") -- empty in almost every modern file; when
    // empty, a batch's textureCoordComboIndex is never dereferenced at all
    // and every layer just uses UV set 0, same convention
    // M2MaterialInputs::textureCoordCombos (export_texture_resolution.hpp)
    // documents.
    std::vector<uint16_t> textureCoordCombos;
    std::vector<m2::Color> colors;
    std::vector<m2::TextureWeight> textureWeights;
    std::vector<uint16_t> textureWeightCombos;
    std::vector<m2::TextureTransform> textureTransforms;
    std::vector<uint16_t> textureTransformCombos;
    // For resolving a genuinely-animated M2Color/M2TextureWeight/
    // M2TextureTransform curve (colorAnimated/alphaAnimated/weightAnimated/
    // translationAnimated/rotationAnimated/scalingAnimated) into real
    // keyframe data for one specific sequence -- same MD20 bytes
    // `colors`/`textureWeights`/`textureTransforms` above were parsed from
    // (M2Track offsets are relative to it, not the .skin file). Left
    // unset (nullptr) only by a hypothetical caller that never populated
    // it, same convention M2MaterialInputs::blob
    // (export_texture_resolution.hpp) documents -- when unset, every
    // *Animated track is left unresolved (MaterialLayer::tint/alphaFade/
    // uvAnimation stay nullopt) rather than dereferencing a null blob.
    const std::vector<uint8_t>* blob = nullptr;
};

// Mirrors buildMaterialsAndPrimitives's per-batch material construction
// (export_materials.cpp) for exactly one real skin::Batch (`batchIndex`
// used only in thrown diagnostic messages, matching that function's own
// "batch " + std::to_string(bi) + ... wording): resolves the batch's
// `materialIndex` -> m2::Material (blendMode -> BlendOp, shared by every
// layer of this batch -- see canon_material_builder.cpp's own doc comment
// on blendModeToBlendOp for why a per-layer Combiners_* decode isn't
// attempted here), then one canon::MaterialLayer per real M2 texture unit
// (`batch.textureCount`, 0..4 per wowdev.wiki M2/.skin#Texture_units) --
// layer i's real combo index is `textureComboIndex + i`, same "base index,
// consecutive layers" convention export_materials.cpp's own
// additionalTextureLayers loop uses.
//
// Throws std::runtime_error on the same corruption cases
// buildMaterialsAndPrimitives does for its own *required* per-batch fields
// (an out-of-range materialIndex, textureComboIndex, or resolved texture
// index for layer 0) -- never silently misreads foreign data.
// Layers beyond the first (i >= 1) are best-effort, same as
// export_materials.cpp's own additionalTextureLayers loop: an out-of-range
// combo/texture index for one of those simply stops emitting further
// layers rather than failing the whole batch, since this is supplementary
// multi-texture metadata, not required for a usable material. Likewise a
// batch's colorIndex/textureWeightComboIndex/textureTransformComboIndex are
// each resolved with the exact same required-vs-best-effort strictness
// export_materials.cpp applies to them (colorIndex/textureWeightComboIndex
// are required *when present*, i.e. not the 0xFFFF/empty-table "none"
// sentinel; textureTransformComboIndex is best-effort even when present).
//
// `sequenceIndex` scopes every *Animated curve this resolves to one
// specific m2::Sequence array index, mirroring assembleBoneAnimation's own
// inline-sequence-only scope (canon_animation_builder.hpp) -- alias
// resolution and global-sequence tracks are the same out-of-scope,
// separate later work that function's own doc comment already states for
// bone tracks, applied here identically to material tracks. This is a
// real, intentional divergence from export_materials.cpp's own
// resolveAnimatedColorCurve/resolveAnimatedFixed16Curve/
// resolveAnimatedRawQuatCurve calls, which each resolve *every* sequence
// (plus a synthetic global-sequence entry) into one flat curve list per
// batch, not a single sequence's worth -- canon::MaterialLayer::tint/
// alphaFade/uvAnimation each hold exactly one canon::Curve (a single
// SequenceRef), not a list, so one call here answers "what does this
// layer's tint/fade/UV-transform look like during sequence N," the same
// per-sequence question assembleBoneAnimation already answers for bones.
//
// Note a real gap surfaced while building this: canon::MaterialLayer has
// no field at all for a *static* (non-animated) tint/alpha value -- only
// the animated-curve shape (`tint`/`alphaFade`) exists. The overwhelming
// common case in real M2 data is a *constant* M2Color/M2TextureWeight
// (colorAnimated/alphaAnimated both false), which export_materials.cpp
// folds into gltf::Material::baseColorFactor -- canon::Material has no
// equivalent slot to receive that value at all today. This function
// intentionally leaves `tint`/`alphaFade` unset (nullopt) for the constant
// case (i.e. exactly "no data" and "static, unrepresentable" alias to the
// same nullopt) rather than guessing at a new field to add -- see this
// task's own report for why canon_material.hpp wasn't silently extended to
// cover it.
//
// `textureResolutions` (default empty): a caller-supplied, already-resolved
// lookup (see `TextureResolutions`'s own doc comment above for the key).
// A key present in the map wins outright -- that layer's TextureRef becomes
// the supplied value verbatim, whatever state it carries, and this
// function performs no resolution logic of its own on it. A missing key
// (including every case when the map is left at its default empty value)
// falls back to the original unconditional
// `TextureRef::State::KnownUnresolved`, byte-for-byte unchanged from this
// parameter's absence -- existing callers that don't opt in see no
// behavior change.
Material assembleMaterial(const skin::Batch& batch, size_t batchIndex, const M2MaterialInputs& m2,
                           uint32_t sequenceIndex, const TextureResolutions& textureResolutions = {});

}  // namespace husk::canon
