#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../export_texture_resolution.hpp"  // FuzzyTexturePool
#include "resolved.hpp"

// REFACTOR/RESOURCE_CATALOG.md's stage-2 resolution boundary, texture half
// (`AUDIT.md` §1.1). The object this document has been missing: the four
// texture tiers already existed as separate pieces (`texture_catalog.hpp`'s
// literal/listfile/fuzzy-read wrappers, `cmd_export.cpp`'s knowledge-base
// tier) with no single object owning the *order* they're tried in, or the
// mutable pool state tier 3's claim-and-remove step needs. This is that
// object, scoped to what `RESOURCE_CATALOG.md`'s "Normative tier order"
// section actually settles (literal -> listfile -> fuzzy same-basename
// pool) plus tier 4's deliberate gap -- see `texture()`'s own doc comment
// for what's deliberately NOT here yet (tier 4 itself, and tier 5/knowledge
// base).
namespace husk::sources {

// I8/BUNDLE_FORMAT.md's "Texture encoding": the catalog hands back whatever
// payload it read, tagged, never silently assuming one shape. Every real
// tier today still decodes BLP -> PNG before the bytes ever reach this type
// (`husk::commands::readTextureFileBytes`, unchanged by this file) -- so in
// practice every `EncodedTexture` this catalog produces is currently tagged
// `Png`. `Blp` exists so the type can represent the un-decoded payload once
// that decode is moved to a writer (I8: "convert on output, never on
// intake") -- adding the tag now, even with one producer, is what makes
// that move a call-site change later instead of a type change.
enum class TextureEncoding { Png, Blp };

struct EncodedTexture {
    std::vector<uint8_t> bytes;
    TextureEncoding encoding = TextureEncoding::Png;
    // Display name derived by whichever tier answered (the source file's own
    // stem) -- not part of I8's {bytes, encoding} payload pair itself, but
    // every real caller (gltf::Material::baseColorImageName,
    // --slim-textures' written filename) needs a name paired with the
    // bytes, and `Resolved<T>::reason` is free text, not meant to be parsed
    // back out for this. `matchedFilename` (with extension) is set
    // alongside `imageName` (without) only for tier 3 hits -- the two real
    // diagnostics that need it (`BuiltMaterials::FuzzyMatch`/
    // `AmbiguousMatch`) are themselves tier-3-only.
    std::string imageName;
    std::string matchedFilename;
};

// Everything `texture()` needs to know about the model asking, beyond the
// fdid/textureType being asked about.
struct TextureModelContext {
    std::string modelPath;
    // The M2's own texture-array FileDataIDs (M2MaterialInputs::
    // textureFileDataIds when present) -- used once, on first sight of this
    // model, to exclude same-basename pool candidates that are already a
    // different, specifically-identified texture belonging to *this* exact
    // M2 (see catalog.cpp's own doc comment; this is the
    // `ethereal2_f.m2` particle-sprite-vs-hardcoded-slot bug
    // `export_materials.cpp` used to guard against inline).
    std::vector<uint32_t> ownTextureFileDataIds;
    // The real M2 texture-array index (m2::Texture[]) this call resolves --
    // NOT a caller-invented id. Two batches can reference the identical M2
    // texture entry (a real case: `argustalbukmount.m2`'s monster_1 texture
    // is used by both a body batch and a separate horns-geoset batch); they
    // must agree on one answer, not each independently claim-and-deplete
    // the shared pool. `Catalog` memoizes `texture()`'s result per
    // (modelPath, textureSlotIndex) for exactly this reason -- see
    // catalog.cpp.
    uint16_t textureSlotIndex = 0;
};

// The data a DB2-character texture tier between listfile and the fuzzy pool
// needs, already resolved by the caller before `Catalog::texture()` is ever
// asked about a slot -- `cmd_export.cpp` already runs
// `attachCharTextureLayout`/`attachCustomizationChoices` (into
// `gltf::Skeleton::charTextureLayout`/`enabledMaterials`) ahead of the LOD
// build loop that owns this `Catalog` instance, so this struct is built
// from that output rather than a second DB2 load or a reimplementation of
// `chrcustomization::resolveChoice`'s own relatedChoiceId filtering --
// `sources::` stays free of any DB2/gltf dependency of its own, and the one
// real place that chain is resolved stays the one place it's resolved (I2).
// Both maps are already scoped to this model's own real
// `CharComponentTextureLayoutsID`/resolved `ChrCustomizationChoiceID`
// selection -- `Catalog` does no further scoping of its own.
struct CharacterTextureContext {
    // M2 textureType -> ChrModelTextureTargetID, present only for a
    // (this model's layout, textureType) pair with exactly one live
    // `ChrModelTextureLayer` row. This has to be a real per-model runtime
    // count, not a hardcoded list of "known good" types: a character skin
    // (type 1) can carry over a dozen independently-selectable texture
    // layers tiling one shared atlas (Skin Color, Face, Hair Style, Tattoo
    // Color, Bracelets, ...), so there is no single fdid to prefer for it,
    // and other types that happen to carry only one layer on one model's
    // layout are not guaranteed to elsewhere -- only a model's own live row
    // count can tell the two cases apart. Absent for every other type.
    std::unordered_map<uint32_t, uint32_t> singleLayerTargetByTextureType;
    // ChrModelTextureTargetID -> resolved real texture FileDataID, from
    // this export's own resolved customization choices -- already filtered
    // for Element::relatedChoiceId conditions and resolved through
    // texturefiledata.db2 (see export_extras.cpp's attachCustomizationChoices).
    // A target present here with `fileDataId == 0` is a real, distinct miss
    // (a material element exists but TextureFileData.db2 didn't resolve
    // it) -- never silently dropped, but never treated as a fdid to try
    // either (fdid 0 has no literal/listfile answer by construction).
    std::unordered_map<uint32_t, uint32_t> fileDataIdByTarget;
};

// Constructed once per export (or once per `--from-list` batch -- never per
// model; per-model state lives inside the catalog, keyed by
// `TextureModelContext::modelPath`, not in the object's own lifetime).
class Catalog {
public:
    Catalog(std::string texturesDir, std::unordered_map<uint32_t, std::string> listfile, std::string listfileRoot,
            std::string texturesOutDir);

    // Opt-in, best-effort DB2-character tier (see `CharacterTextureContext`'s
    // own doc comment) -- absent by default, a clean no-op for every model
    // this wasn't set for (most models, most invocations have no derivable
    // player-character identity at all). Applies to every slot this
    // `Catalog` resolves from here on, not per-model scoped -- one `Catalog`
    // instance is already one model export (see the class's own doc comment
    // above), so there is only ever one real context to set.
    void setCharacterTextureContext(CharacterTextureContext ctx);

    // RESOURCE_CATALOG.md's "Normative tier order", the real object owning
    // it: literal `<texturesDir>/<fdid>.{png,blp}` -> `--listfile` ->
    // DB2-character (opt-in, see `setCharacterTextureContext`) ->
    // same-basename fuzzy pool (claim-and-remove, now catalog-owned --
    // RESOURCE_CATALOG.md's Settled section, "Tier 3's shape": the three-way
    // branch a caller used to see -- claimed-and-read / nothing-claimed-so-
    // scan-for-ambiguity / 2+ candidates -- collapses into this one call).
    // The DB2-character tier produces a fdid, never bytes of its own --
    // read via the same `resolveLiteralTier`/`resolveListfileTier` calls
    // tier 1/2 already make (never a fourth way to turn a fdid into bytes),
    // just against a DB2-derived fdid instead of the slot's own M2 one.
    //
    // Deliberately does NOT include:
    // - Tier 4 (parent-directory same-basename). Blender-script-only today
    //   (`husk_blender_geoset_mask.py:1575-1596`); porting it changes real
    //   resolution outcomes for real files and needs its own ledger run --
    //   out of this object's first pass. `RESOURCE_CATALOG.md`'s "Where the
    //   two unordered tiers sit" is where it goes once it lands.
    // - Tier 5 (knowledge base). `resolveObjectSkinTextureFromKb`
    //   (`cmd_export.cpp`) answers a genuinely different question --
    //   "which FileDataID should stand in for this model's object-skin
    //   slot, since the M2 itself gives none" -- not "given this fdid,
    //   find bytes". It stays a pre-step the caller runs once per model,
    //   feeding its answer back through the normal fdid parameter here
    //   exactly as before. Injecting its answer is this object's job, not
    //   the caller's: see `registerPathOverride`, below.
    Resolved<EncodedTexture> texture(uint32_t fdid, uint32_t textureType, const TextureModelContext& model,
                                      bool preferGlowVariant = false);

    // Registers a direct fdid -> real-content-path answer the tier-2
    // (listfile) lookup should treat as if it were a real listfile row --
    // without mutating the actual `--listfile` map the catalog was
    // constructed with -- a caller may hold that map by reference and
    // share it with other consumers, so a per-model answer written into it
    // would leak. An override always wins over a
    // real listfile row for the same fdid -- there should never be a real
    // collision (the object-skin slot's fdid is husk-derived, not a real
    // M2-embedded one), but if one ever happened, trusting the more
    // specific, deliberately-registered answer over an incidental listfile
    // row is the safer default.
    void registerPathOverride(uint32_t fdid, std::string realContentPath);

    // How many same-basename pool candidates are still unclaimed for
    // `modelPath` -- the diagnostic `buildMaterialsAndPrimitives` used to
    // read straight off its own local `FuzzyTexturePool` (now owned here
    // instead). Returns 0 for a model this catalog has never scanned a pool
    // for.
    size_t remainingTexturePoolSize(const std::string& modelPath) const;

    // I4's ledger -- one line per distinct (model, textureSlotIndex) this
    // instance has resolved, in resolution order. A read operation: no
    // control-flow decision anywhere in this file depends on it.
    std::string describe() const;

    // One line per distinct (model, textureSlotIndex) resolved so far, in
    // resolution order -- the same data `describe()` renders as prose,
    // structured for a consumer that needs to tell "tier 2 answered" from
    // "tier 3 guessed among 218 candidates" programmatically (I4, I6;
    // `husk resolve`, REFACTOR/CLI_AND_TOOLING.md §3) instead of parsing
    // `describe()`'s own free text back apart. A read operation, same as
    // `describe()` -- no control-flow decision anywhere in this file
    // depends on it.
    struct LedgerEntry {
        std::string modelPath;
        uint16_t textureSlotIndex = 0;
        uint32_t fileDataId = 0;
        uint32_t textureType = 0;
        bool found = false;
        ResolutionTier tier = ResolutionTier::Miss;
        std::string reason;
        size_t byteCount = 0;
        size_t alternateCount = 0;
        // The resolved image's own name (EncodedTexture::imageName on a
        // hit, empty on a miss) -- "the resolved path or filename" a
        // structured consumer needs alongside the tier, not reconstructed
        // by parsing `reason`'s free text.
        std::string resolvedName;
    };
    const std::vector<LedgerEntry>& ledger() const { return ledger_; }

private:
    struct ModelState {
        husk::commands::FuzzyTexturePool pool;
        bool poolInitialized = false;
        std::unordered_map<uint16_t, Resolved<EncodedTexture>> slotCache;
    };

    ModelState& stateForModel(const TextureModelContext& model);
    Resolved<EncodedTexture> resolveLiteralTier(uint32_t fdid) const;
    Resolved<EncodedTexture> resolveListfileTier(uint32_t fdid) const;
    Resolved<EncodedTexture> resolveDb2CharacterTier(uint32_t textureType) const;
    Resolved<EncodedTexture> resolveFuzzyTier(ModelState& state, uint32_t textureType,
                                               const std::string& modelPath, bool preferGlowVariant);
    // `--listfile-root` keeps defaulting to `--textures` (RESOURCE_CATALOG.md's
    // Settled section) -- resolved once here, not re-derived per caller.
    std::optional<std::filesystem::path> listfileTargetPath(uint32_t fdid) const;

    std::string texturesDir_;
    std::unordered_map<uint32_t, std::string> listfile_;
    std::string listfileRoot_;
    std::string texturesOutDir_;
    std::unordered_map<uint32_t, std::string> pathOverrides_;
    std::optional<CharacterTextureContext> characterContext_;

    std::unordered_map<std::string, ModelState> modelStates_;
    // Shared across every model this catalog instance ever resolves an
    // ambiguous batch for, not just one model's own batches -- see
    // export_texture_resolution.cpp's own `orderCandidatesForDefault` doc
    // comment for the redundant-decode regression this avoids (1,786
    // redundant BLP decodes on one real file before this cache existed);
    // keyed by absolute path, so sharing it across models is strictly more
    // effective than the original per-`buildMaterialsAndPrimitives`-call
    // cache, never less correct (a given real file decodes to the same
    // bytes regardless of which model's batch asked first).
    std::map<std::filesystem::path, std::vector<uint8_t>> ambiguousCandidateCache_;
    std::vector<LedgerEntry> ledger_;
};

}  // namespace husk::sources
