#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "canon_curve.hpp"
#include "canon_ref.hpp"

// husk::canon: see canon_policy.hpp for the layer this belongs to.
// DESIGN.md's "canon:: material design" -- a pure value type, no consumers
// wired to it yet (migrating gltf_mesh.hpp's Material/export_materials.cpp
// onto it, and porting real Combiners_* formula math into
// ShadingFunction::function, are both separate, later work).
namespace husk::canon {

// M2 (Legion+) has no PBR map slots anywhere in the parsed format -- these
// are the roles WoW's own data can actually assert (DESIGN.md's "real facts
// checked before designing anything"), not an invented PBR vocabulary. The
// string alternative keeps this open past M2's own vocabulary for a future
// producer without forcing a KnownRole enumerator to exist for it.
enum class KnownRole { Unknown, Diffuse, Specular, Emission, Alpha, Detail, Env };
using LayerRole = std::variant<KnownRole, std::string>;

// A layer's UV source. The real client never does compatibility matching --
// skin::Batch::textureCoordComboIndex is a hardcoded literal resolved once
// at authoring time -- so this is a typed selector, not a scored matcher.
struct UvSetIndex {
    uint32_t index = 0;  // which of the mesh's real UV sets (today: 0 or 1)
};
struct EnvironmentMapped {};  // textureCoordComboIndex == -1: a runtime-computed reflection vector, no stored per-vertex UV
using UvRef = std::variant<UvSetIndex, EnvironmentMapped>;

// BUNDLE_FORMAT.md's "Texture encoding -- settled": canonical storage is the
// source *payload* (compressed GPU blocks, rehoused verbatim into an open
// container -- DDS), never a proprietary re-encode and never raw decoded
// pixels. Png exists for the projection a writer generates on request (e.g.
// glTF, which has no DDS/BC slot). Every real producer today still only
// ever populates Png (`sources::Catalog`'s own intake still decodes BLP ->
// PNG before bytes reach anything canon:: touches -- see catalog.hpp's own
// doc comment) -- the Bc1/Bc2/Bc3/Bgra/Palettized tags exist so this type
// doesn't need a second breaking change once that decode moves to a writer
// (I8: "convert on output, never on intake"), not because a producer emits
// them yet.
enum class TextureEncoding { Png, Bc1, Bc2, Bc3, Bgra, Palettized };

// Three states, not two -- "resolved" isn't the only non-error outcome, same
// three-state resolution DESIGN.md's §2.11 already establishes for texture
// lookups elsewhere in this project.
struct TextureRef {
    enum class State { Resolved, KnownUnresolved, Ambiguous };
    State state = State::KnownUnresolved;

    // Meaningful only when state == Resolved: real FileDataID/name in hand.
    Ref resolved;

    // Meaningful only when state == Resolved. Absent even then: BUNDLE_FORMAT.md's
    // "Embed or reference -- one rule" -- a resolved identity does not imply
    // a producer chose to fetch and carry real bytes. Present means "embed
    // this payload"; absent (with `resolved` still populated) means "this
    // resource lives in a reference target" -- a writer that finds `resolved`
    // but no `payload` emits a `uri`-only entry (or, if it has none to give
    // either, an id-only "husk knows what this is, couldn't produce it"
    // entry -- BUNDLE_FORMAT.md's "a reference may be unresolved"), never
    // silently drops the material layer. Whether to fetch bytes at all is a
    // producer-time decision (same place `TextureResolutions` itself gets
    // built, `cmd_export_canon.cpp`), not something canon:: decides for
    // itself.
    struct Payload {
        std::vector<uint8_t> bytes;
        TextureEncoding encoding = TextureEncoding::Png;
    };
    std::optional<Payload> payload;

    // Meaningful only when state == Resolved, and independent of `payload`
    // above -- a second, additive representation, not a replacement.
    // BUNDLE_FORMAT.md's "Texture encoding -- settled" section says a
    // texture resource entry "may carry more than one variant of the same
    // texture": `payload` is the PNG projection `gltf_lean.cpp` embeds as a
    // core-glTF image (PNG/JPEG only, by the glTF spec itself -- DDS is not
    // a valid glTF image format), while `rawPayload` is the lossless
    // source-payload variant (compressed GPU blocks, rehoused verbatim into
    // a DDS container, `blp::extractRawPayload`+`blp::encodeDds`) that
    // `bundle_writer.cpp` writes as `textures/<name>.dds` per this file's
    // own settled design. One field can't serve both consumers: gltf_lean
    // would have to either embed invalid non-PNG bytes as if they were PNG
    // (rejected by any validator/importer) or transcode DDS blocks back to
    // pixels at write time, which needs a real DDS/BC-block decoder this
    // codebase does not have and is not the job of a glTF writer to grow.
    // Kept genuinely independent (a producer may populate one, both, or
    // neither) rather than "computed from the other at write time" so a
    // writer never has to guess a producer's intent from which one is set.
    std::optional<Payload> rawPayload;

    // Meaningful only when state == KnownUnresolved: M2 asserts a real slot
    // exists (nonzero TextureType, or a customization-driven slot) but husk
    // couldn't get bytes for it -- same backtrace pattern
    // resolveObjectSkinTextureFromKb's `.reason` already uses elsewhere.
    std::string unresolvedReason;

    // Meaningful only when state == Ambiguous: N real candidates, no
    // in-file tiebreak -- generalizes today's gltf::Material::
    // AlternateTextureCandidate, kept as the full candidate set (not
    // collapsed to one guess) so a consumer can pick, or a human can
    // resolve it later. Deliberately excludes AlternateTextureCandidate's
    // own filename/imagePng fields: a path-shaped string has no place in
    // canon:: (I1), and an ambiguous slot has no single payload to carry --
    // resolving the ambiguity (or not) is what turns this into a real
    // Resolved payload, not a fact this diagnostic state should guess at.
    struct Candidate {
        Ref identity;          // FileDataId + whatever name source resolved it (I6)
        std::string category;  // export_materials.cpp's classifyCandidateCategory vocabulary; empty if unclassified
        uint32_t width = 0;    // real decoded pixel dimensions -- load-bearing for ranking, see AlternateTextureCandidate's own doc comment
        uint32_t height = 0;
    };
    std::vector<Candidate> candidates;
};

// EGxTexOp (wowdev.wiki Rendering.md "Texture Blending" -- verified across
// several real clients per that page's own credits, not the flagged-
// unverified "four base functions" recollection below). The real
// fixed-function operation the client issues to fold one texture unit into
// the stack built up so far. Reused for both MaterialLayer::blendIntoPrevious
// and Function::Stage below so there is exactly one texture-combine
// vocabulary in this file, not two that could drift apart.
enum class BlendOp { Modulate, Modulate2x, Add, Replace, Decal, Fade };

struct MaterialLayer {
    Ref identity;  // stable identity a named slot points at -- never a raw vector index, same reasoning already applied to bones/geosets
    TextureRef texture;
    UvRef uv;
    LayerRole role;
    std::optional<VecCurve> tint;
    std::optional<ScalarCurve> alphaFade;

    // A batch's M2TextureTransform (wowdev.wiki M2#Texture_Transforms),
    // each component independently animated via canon::Curve -- the shape
    // DESIGN.md calls out as "TBD at implementation time"; this is that
    // choice, justified by direct 1:1 correspondence to M2TextureTransform's
    // three real fields, no more and no less.
    struct TextureTransformCurves {
        std::optional<VecCurve> translation;
        std::optional<QuatCurve> rotation;
        std::optional<VecCurve> scaling;
    };
    std::optional<TextureTransformCurves> uvAnimation;

    // GxTexBlend_Opaque's own ColorOp (GL_MODULATE) is the client's real
    // fallback when nothing overrides it, so it's the honest default here
    // too -- not an arbitrary placeholder value.
    BlendOp blendIntoPrevious = BlendOp::Modulate;
};

struct Material {
    // This material's own identity -- what a primitive's entry in
    // Model::primitiveMaterials points at (canon_model.hpp's own doc
    // comment). `ref.id` is `RecordIndex{i}` (this material's own position
    // in whichever Model::materials vector holds it) for every real
    // producer today -- a purely local model has no other identity to
    // give a material. Other `Identity` kinds (FileDataId, Db2Row) are
    // reserved for a future bundle-external, cross-model-shared material
    // (BUNDLE_FORMAT.md/I3 territory) -- preserving that possibility is
    // required, implementing it is not (I7's own "preserve, don't
    // implement" precedent, applied here).
    Ref ref;
    std::vector<MaterialLayer> layers;  // full ordered truth, one entry per real M2 texture unit
    std::optional<Ref> diffuseLayer;    // = some layer's identity, only when role maps cleanly
    std::optional<Ref> specularLayer;
    std::optional<Ref> emissionLayer;
    std::optional<Ref> alphaLayer;
    // no normalLayer/roughnessLayer yet -- no real producer populates one; add when one actually exists, not speculatively.
};

// The framebuffer-blend axis the "four base functions" hypothesis
// (DESIGN.md's "Shading-function descriptions: a scaffold, not a one-shot
// transcription", explicitly flagged UNVERIFIED -- Luna's own recollection
// of a different, uncorroborated AI session, not this project's own
// confirmed finding) predicts most of M2's ~20 named Combiners_* formulas
// reduce to.
struct AlphaClip {};
struct AlphaBlend {};
struct Additive {};
// tintSource intentionally omitted: DESIGN.md is explicit this parameter's
// real shape is exactly the open transcription work, not yet done --
// adding a field here would be guessing at data this design doesn't have.
struct Multiplicative {};
using BaseFunction = std::variant<AlphaClip, AlphaBlend, Additive, Multiplicative>;

// A different axis from TextureRef::State -- texture *resolution* trust and
// shading-formula *mapping* trust are unrelated facts about unrelated
// producers, so they get unrelated vocabularies (same reasoning NameSource
// and TextureRef::State already get kept apart for).
enum class Confidence { Verified, Assumed, Ambiguous, GenuinelyUnknown };

// Function's own ground-truth representation. Deliberately a flat ordered
// chain of BlendOp stages, not a tree: M2's own texture stack
// (M2Batch.textureCount) is already an ordered sequence, not a graph, and
// GxTexOp's own Arg0 == GL_PREVIOUS convention already describes "fold this
// unit into the running result," which a flat chain expresses directly with
// no recursion. Deliberately minimal too (color-combine op only, no
// separate alpha-op/scale/arg2) since no real formula has been transcribed
// into this yet (TODO/MULTI_TEXTURE_LAYER_TODO.md) -- extend when a real
// transcribed formula actually needs more, not speculatively. An empty
// `stages` means "not transcribed" (GenuinelyUnknown territory), the same
// meaning an empty vector already carries elsewhere in this codebase (e.g.
// Curve::keyframes).
struct Function {
    struct Operand {
        enum class Kind { PreviousStage, Layer, Constant };
        Kind kind = Kind::PreviousStage;
        uint32_t layerIndex = 0;  // meaningful only when kind == Layer: index into Material::layers
    };
    struct Stage {
        BlendOp op = BlendOp::Modulate;
        Operand arg0;
        Operand arg1;
    };
    std::vector<Stage> stages;
};

struct ShadingFunction {
    // e.g. name = "Combiners_Mod_Add" -- husk derives this string
    // algorithmically from shaderId (m2::resolveShaderNames), no embedded
    // M2 string ever names it, so NameSource::Synthesized is the honest
    // source for a real producer to set here, not M2Embedded.
    Ref identity;
    // AlphaClip is a placeholder default, not a considered choice -- picking
    // the real documented fallback for a GenuinelyUnknown formula is
    // corpus-frequency-driven work for whoever does the transcription
    // (DESIGN.md's "Shading-function descriptions"), not decided here.
    // Spelled out explicitly so a default-constructed value reads as a
    // stated placeholder rather than an accident of variant's first-
    // alternative rule.
    BaseFunction base = AlphaClip{};
    Confidence confidence = Confidence::GenuinelyUnknown;
    Function function;  // ground truth; math/description below are views of it, not independent sources
    std::optional<std::string> math;
    std::optional<std::string> description;
};

}  // namespace husk::canon
