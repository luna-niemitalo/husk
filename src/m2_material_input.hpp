#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "canon_material.hpp"
#include "canon_options.hpp"  // TextureSlot
#include "m2_animation.hpp"  // m2::Color/TextureWeight/TextureTransform
#include "m2_header.hpp"     // m2::Material/Texture
#include "skin.hpp"          // skin::Batch

// husk::m2input: the M2 input module -- see m2_canon_input.hpp's own doc
// comment for the split this belongs to. Assembles one canon::Material (and
// its canon::MaterialLayer stack) from a real .skin Batch plus the M2's own
// material/texture/color/transform tables.
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
namespace husk::m2input {

// One already-resolved (or explicitly failed/ambiguous) texture lookup,
// resolved entirely outside canon:: by the caller -- see this header's own
// top comment and REFACTOR/AUDIT.md's §7.1. Keyed by the same M2 texture
// array index `MaterialLayer::identity` already uses as its own
// `RecordIndex` (m2_material_input.cpp's `layerIdentity`/
// `assemblePrimaryLayer`'s `textureIndex` -- i.e. the resolved index into
// `M2MaterialInputs::textures`, NOT a raw `skin::Batch`/`textureCombos`
// position) -- reusing that existing identity key instead of inventing a
// second one for the same texture unit.
using TextureResolutions = std::unordered_map<uint32_t, canon::TextureRef>;

// Everything assembleMaterial needs out of the M2 itself, bundled (one call
// site, many arrays, no real abstraction cost).
struct M2MaterialInputs {
    std::vector<m2::Material> materials;
    std::vector<m2::Texture> textures;
    std::vector<uint16_t> textureCombos;
    // Pre-Cataclysm only (wowdev.wiki M2/.skin#geosetIndex: "Still present
    // but unused in Cataclysm") -- empty in almost every modern file; when
    // empty, a batch's textureCoordComboIndex is never dereferenced at all
    // and every layer just uses UV set 0.
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
    // it -- when unset, every
    // *Animated track is left unresolved (MaterialLayer::tint/alphaFade/
    // uvAnimation stay nullopt) rather than dereferencing a null blob.
    const std::vector<uint8_t>* blob = nullptr;
};

// One real skin::Batch's material (`batchIndex` used only in thrown
// diagnostic messages): resolves the batch's
// `materialIndex` -> m2::Material (blendMode -> BlendOp, shared by every
// layer of this batch -- see m2_material_input.cpp's own doc comment on
// blendModeToBlendOp for why a per-layer Combiners_* decode isn't
// attempted here), then one canon::MaterialLayer per real M2 texture unit
// (`batch.textureCount`, 0..4 per wowdev.wiki M2/.skin#Texture_units) --
// layer i's real combo index is `textureComboIndex + i` ("base index,
// consecutive layers").
//
// Throws std::runtime_error on corruption of the *required* per-batch
// fields (an out-of-range materialIndex, textureComboIndex, or resolved
// texture index for layer 0) -- never silently misreads foreign data.
// Layers beyond the first (i >= 1) are best-effort: an out-of-range combo
// index stops further layers and an out-of-range texture index skips that
// layer, rather than failing the whole batch, since this is supplementary
// multi-texture metadata, not required for a usable material. Likewise
// colorIndex/textureWeightComboIndex are required *when present* (i.e. not
// the 0xFFFF/empty-table "none" sentinel), and textureTransformComboIndex
// is best-effort even when present.
//
// `sequenceIndex` scopes every animated curve to one m2::Sequence array
// index, the same per-sequence question assembleBoneAnimation answers for
// bones: canon::MaterialLayer::tint/alphaFade/uvAnimation each hold exactly
// one canon::Curve (a single SequenceRef), not a list. A track driven by a
// global loop gets a global-sequence curve instead (the same keys under
// every sequence). A constant (non-animated) value other than the identity
// (white tint, full alpha, no transform) becomes a one-key step curve; the
// identity leaves the field unset.
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
canon::Material assembleMaterial(const skin::Batch& batch, size_t batchIndex, const M2MaterialInputs& m2,
                                  uint32_t sequenceIndex, const TextureResolutions& textureResolutions = {});

// One slot per distinct replaceable texture type (nonzero M2 texture
// type) in `textures`, first-seen order: the slots a texture option may fill.
std::vector<canon::TextureSlot> textureSlots(const std::vector<m2::Texture>& textures);

}  // namespace husk::m2input
