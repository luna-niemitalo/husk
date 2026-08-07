#include "export_materials.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <unordered_map>

#include "blp.hpp"

namespace husk::commands {

namespace {

// Writes `pngBytes` to `texturesOutDir`, mirroring `path`'s location
// relative to `texturesDir` (so a real recursive --textures scan, if one
// ever exists, stays mirrored too -- today's flat scan just means a single
// path segment). Best-effort: a write failure is reported and otherwise
// ignored, since --textures-out is a convenience copy, not the thing the
// export itself depends on (the in-memory bytes are already embedded
// regardless of whether this succeeds).
void writeTextureOutCopy(const std::filesystem::path& path, const std::string& texturesDir,
                          const std::string& texturesOutDir, const std::vector<uint8_t>& pngBytes) {
    if (texturesOutDir.empty()) return;
    std::error_code ec;
    auto rel = std::filesystem::relative(path, texturesDir, ec);
    if (ec) rel = path.filename();
    rel.replace_extension(".png");
    auto outPath = std::filesystem::path(texturesOutDir) / rel;
    std::filesystem::create_directories(outPath.parent_path(), ec);
    std::ofstream out(outPath, std::ios::binary);
    if (!out) {
        std::cout << "husk: warning: couldn't write '" << outPath.string() << "' (--textures-out)\n";
        return;
    }
    out.write(reinterpret_cast<const char*>(pngBytes.data()), static_cast<std::streamsize>(pngBytes.size()));
}

// Reads a texture file's embeddable PNG bytes directly -- `.png` as-is,
// `.blp` decoded and PNG-re-encoded in memory (see src/blp.hpp; this is the
// only reason `husk export` used to need a separate `husk-blp` conversion
// step first). Never writes an intermediate file unless `texturesOutDir` is
// given (--textures-out), in which case a freshly-decoded `.blp` also gets
// mirrored there as a real `.png` -- a `.png` source is never re-copied,
// nothing new was decoded for it. Returns nullopt if the file can't be
// opened or (for `.blp`) fails to decode -- a decode failure is reported
// once here and treated as "no texture available for this slot," same as a
// missing file, not a hard export failure.
std::optional<std::vector<uint8_t>> readTextureFileBytes(const std::filesystem::path& path,
                                                          const std::string& texturesDir,
                                                          const std::string& texturesOutDir) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (path.extension() != ".blp") return raw;
    try {
        auto png = blp::encodePng(blp::decode(raw));
        writeTextureOutCopy(path, texturesDir, texturesOutDir, png);
        return png;
    } catch (const blp::ParseError& e) {
        std::cout << "husk: warning: failed to decode '" << path.string() << "': " << e.what() << "\n";
        return std::nullopt;
    }
}

// Tries `<stemPath>.png` first, falling back to `<stemPath>.blp` -- the
// same "PNG wins if both exist" priority scanFuzzyTexturePool's dedup uses,
// so a directory containing both an already-converted PNG and its source
// BLP never decodes the BLP redundantly.
std::optional<std::vector<uint8_t>> resolveTextureBytes(const std::filesystem::path& stemPath,
                                                          const std::string& texturesDir,
                                                          const std::string& texturesOutDir) {
    auto pngPath = stemPath;
    pngPath += ".png";
    if (auto bytes = readTextureFileBytes(pngPath, texturesDir, texturesOutDir)) return bytes;
    auto blpPath = stemPath;
    blpPath += ".blp";
    return readTextureFileBytes(blpPath, texturesDir, texturesOutDir);
}

// WoW's M2BLEND_* blend modes (wowdev.wiki M2/Rendering#M2BLEND) collapsed
// to glTF's three-way alphaMode: 0 (OPAQUE) maps directly, 1 (ALPHA_KEY,
// alpha-tested) maps to MASK, and everything else -- 2 (a real alpha
// blend), plus the additive/multiply modes 3+ that glTF's core material
// model has no equivalent for -- maps to BLEND as the closest
// approximation.
gltf::Material::AlphaMode alphaModeForBlend(uint16_t blendMode) {
    switch (blendMode) {
        case 0: return gltf::Material::AlphaMode::Opaque;
        case 1: return gltf::Material::AlphaMode::Mask;
        default: return gltf::Material::AlphaMode::Blend;
    }
}

// M2Material flags (wowdev.wiki M2#Render_flags_and_blending_modes). Only
// 0x04 (two-sided) was translated before; 0x01 (unlit) is the other bit
// with a real glTF equivalent (KHR_materials_unlit). depthTest/depthWrite
// (0x08/0x10) have no core-glTF equivalent at all so aren't translated --
// surfaced as raw `flags` on m2::Material for a consumer that wants them.
constexpr uint16_t kMaterialUnlitFlag = 0x01;
constexpr uint16_t kMaterialTwoSidedFlag = 0x04;

// Decodes one raw fixed16 wire value (as resolveRawIntTrackSequence/
// resolveRawIntGlobalSequenceTrack return it, zero-extended into a
// uint32_t) into a 0.0..1.0 float -- the same conversion m2.cpp's
// readFixed16TrackValue uses for the constant-value case (wowdev.wiki
// M2#Colors_and_transparency's own "0 - transparent, 0x7FFF - opaque"
// scale).
float decodeFixed16(uint32_t bits) {
    uint16_t b = static_cast<uint16_t>(bits);
    int16_t raw;
    std::memcpy(&raw, &b, sizeof(raw));
    return std::clamp(static_cast<float>(raw) / 32767.0f, 0.0f, 1.0f);
}

// Resolves a genuinely-animated M2Color::color track (colorAnimated) into
// real (seconds, rgb) keyframes -- one gltf::Material::AnimatedColorCurve
// per M2Sequence that has real inline data for this track, plus a
// synthetic global-sequence entry (sequenceIndex left at -1) when the track
// loops independently of any M2Sequence instead. `color`'s x/y/z are
// already 0..1 RGB, NOT a spatial vector -- deliberately NOT run through
// toGltf()'s Z-up -> Y-up remap.
std::vector<gltf::Material::AnimatedColorCurve> resolveAnimatedColorCurve(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, size_t sequenceCount) {
    std::vector<gltf::Material::AnimatedColorCurve> curves;
    for (size_t si = 0; si < sequenceCount; ++si) {
        auto raw = m2::resolveVec3TrackSequence(blob, trackOffset, static_cast<uint32_t>(si));
        if (raw.empty()) continue;
        gltf::Material::AnimatedColorCurve curve;
        curve.sequenceIndex = static_cast<int>(si);
        curve.keyframes.reserve(raw.size());
        for (const auto& [ts, v] : raw) {
            curve.keyframes.emplace_back(static_cast<float>(ts) / 1000.0f, gltf::Vec3{v.x, v.y, v.z});
        }
        curves.push_back(std::move(curve));
    }
    auto global = m2::resolveVec3GlobalSequenceTrack(blob, trackOffset);
    if (!global.empty()) {
        gltf::Material::AnimatedColorCurve curve;
        curve.keyframes.reserve(global.size());
        for (const auto& [ts, v] : global) {
            curve.keyframes.emplace_back(static_cast<float>(ts) / 1000.0f, gltf::Vec3{v.x, v.y, v.z});
        }
        curves.push_back(std::move(curve));
    }
    return curves;
}

// Same shape as resolveAnimatedColorCurve, but for a genuinely-animated
// M2Color::alpha or M2TextureWeight::weight track (both M2Track<fixed16>) --
// shared by both since only the source field differs, see
// gltf::Material::alphaFadeAnimation/weightFadeAnimation's doc comment.
std::vector<gltf::Material::AnimatedScalarCurve> resolveAnimatedFixed16Curve(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, size_t sequenceCount) {
    std::vector<gltf::Material::AnimatedScalarCurve> curves;
    for (size_t si = 0; si < sequenceCount; ++si) {
        auto raw = m2::resolveRawIntTrackSequence(blob, trackOffset, static_cast<uint32_t>(si), 2);
        if (raw.empty()) continue;
        gltf::Material::AnimatedScalarCurve curve;
        curve.sequenceIndex = static_cast<int>(si);
        curve.keyframes.reserve(raw.size());
        for (const auto& [ts, bits] : raw) {
            curve.keyframes.emplace_back(static_cast<float>(ts) / 1000.0f, decodeFixed16(bits));
        }
        curves.push_back(std::move(curve));
    }
    auto global = m2::resolveRawIntGlobalSequenceTrack(blob, trackOffset, 2);
    if (!global.empty()) {
        gltf::Material::AnimatedScalarCurve curve;
        curve.keyframes.reserve(global.size());
        for (const auto& [ts, bits] : global) {
            curve.keyframes.emplace_back(static_cast<float>(ts) / 1000.0f, decodeFixed16(bits));
        }
        curves.push_back(std::move(curve));
    }
    return curves;
}

// Best-effort filename-based resolution for a texture slot, tried before
// any FileDataID-named lookup -- applies to every slot, not just hardcoded
// ones, since a real texture directory isn't always FileDataID-named.
//
// `scanFuzzyTexturePool` finds every '.png' file whose stem
// (case-insensitive) starts with the model's own basename and isn't
// itself a bare FileDataID (that case is already handled by the
// exact-match path above); `claimSoleFuzzyTextureCandidate` hands out the
// pool's sole remaining entry (removing it) only when exactly one is
// left -- the one case where "which slot does this belong to" isn't
// actually ambiguous. Claim-and-remove, not a fresh scan per batch, matters
// here: a real texture directory built for one character typically has
// *several* real files sharing that character's basename -- a fresh scan
// per batch would wire the *same* image into every unresolved slot at
// once. One pool, scanned once per skin/LOD, shared and depleted across
// every batch in that call, is what keeps a real match going to at most
// one slot.
struct FuzzyTexturePool {
    std::vector<std::filesystem::path> files;
};

// Shared by scanFuzzyTexturePool (which files belong to this model's pool
// at all) and classifyCandidateCategory (parsing a category token out of
// one of them) -- one lowercasing, not two independently maintained copies.
std::string lowercaseModelBasename(const std::string& modelPath) {
    std::string basename = std::filesystem::path(modelPath).stem().string();
    std::transform(basename.begin(), basename.end(), basename.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return basename;
}

// Real community-listfile category token embedded in a candidate's own
// filename (e.g. "skin_color" out of "bloodelffemale_hd_skin_color_3500123.
// blp") -- this is metadata a human already attached to the file (the same
// listfile convention `scanFuzzyTexturePool`'s model-basename-prefix match
// already trusts), not husk inferring anything about M2 internals. Returns
// "" for a bare "<modelBasename>_<FileDataID>" file (no category at all),
// or std::nullopt if `path` doesn't even start with the model's own
// basename (shouldn't happen -- scanFuzzyTexturePool already filtered for
// that -- kept so this stays safe to call on its own).
std::optional<std::string> classifyCandidateCategory(const std::filesystem::path& path,
                                                       const std::string& modelBasenameLower) {
    std::string stem = path.stem().string();
    std::transform(stem.begin(), stem.end(), stem.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (stem.rfind(modelBasenameLower, 0) != 0) return std::nullopt;
    std::string rest = stem.substr(modelBasenameLower.size());
    if (!rest.empty() && rest.front() == '_') rest.erase(0, 1);

    auto isAllDigits = [](const std::string& s) {
        return !s.empty() &&
               std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
    };
    if (isAllDigits(rest)) return std::string();  // bare "<modelBasename>_<FileDataID>"

    auto lastUnderscore = rest.find_last_of('_');
    if (lastUnderscore != std::string::npos && isAllDigits(rest.substr(lastUnderscore + 1))) {
        rest.erase(lastUnderscore);  // drop the trailing "_<FileDataID>"
    }
    return rest;
}

// Category token -> the M2Texture::type values (m2::textureTypeName) it's
// actually compatible with. Transcribed, not guessed, from
// `reference/wow.export/src/js/modules/tab_characters.js`: the legacy
// option-group map (~line 1354, `'skin'`/`'face'`/`'hair color'`/
// `'hair style'`/`'facial'`) and the explicit "blindfold = type 9" comment
// on `apply_skinned_model_textures` (~line 791). Types 1 (skin) and 8
// (skin_extra) are the two the same function shows binding a *composite* of
// several blended layers at runtime, not one raw file -- husk has no
// compositing engine (would need real ChrModelTextureLayer blend-order/DB2
// data it deliberately doesn't have, see DESIGN.md's Non-goals), so
// "skin_color"/"face" both stay valid candidates for those two types, but
// see `pickDefaultCandidate` for why the base layer is still preferred over
// the overlay when choosing which one becomes the wired default.
const std::unordered_map<std::string, std::vector<uint32_t>>& candidateCategoryTypes() {
    static const std::unordered_map<std::string, std::vector<uint32_t>> kMap = {
        {"skin_color", {1, 8}},
        {"face", {1, 8}},
        {"hair_color", {6, 22}},
        {"hair_style", {6, 22}},
        {"facial_hair", {7}},
        {"eye_color", {19}},
        {"jewelry_color", {20}},
        {"body_jewelry", {20}},
        {"bracelets", {20}},
        {"blindfold", {9}},
    };
    return kMap;
}

// An unrecognized category (no token match in candidateCategoryTypes() at
// all -- e.g. a non-character model's own plainly-named alternates, or a
// real listfile token this table hasn't been taught yet) is always allowed:
// the old, unfiltered-pool behavior for those, unchanged. A *recognized*
// category naming a different, non-overlapping type is the one real
// exclusion this adds -- e.g. a "hair_color"-tagged file no longer gets
// offered to a "char_jewelry" (type 20) slot just because both slots are
// independently ambiguous.
//
// A bare "<model>_<FileDataID>" candidate (category == "") is the one
// genuinely ambiguous case: on a simple, non-character model it's the only
// kind of candidate that ever exists, so it has to stay a wildcard, but on
// a real character-customization directory (recognized category siblings
// present elsewhere in the same pool -- `bareMeansSkinOnly`, set once per
// model, not guessed per file) it's the real base skin/skin_extra layer
// specifically (confirmed against `bloodelffemale_hd`'s own real export
// directory: the bare files are consistently the small composite-target
// candidates, never a hair/eye/jewelry file), so it's restricted the same
// way "skin_color" is instead of leaking into every other ambiguous slot.
bool candidateAllowedForType(const std::optional<std::string>& category, uint32_t textureType,
                              bool bareMeansSkinOnly) {
    if (!category) return true;
    if (category->empty()) return !bareMeansSkinOnly || textureType == 1 || textureType == 8;
    const auto& map = candidateCategoryTypes();
    auto it = map.find(*category);
    if (it == map.end()) return true;
    return std::find(it->second.begin(), it->second.end(), textureType) != it->second.end();
}

// True when the model's own fuzzy pool has at least one file carrying a
// recognized category token -- see candidateAllowedForType's doc comment
// for why this flips a bare candidate from "wildcard" to "skin-only".
bool poolHasRecognizedCategory(const std::vector<std::filesystem::path>& files,
                                const std::string& modelBasenameLower) {
    const auto& map = candidateCategoryTypes();
    for (const auto& p : files) {
        auto category = classifyCandidateCategory(p, modelBasenameLower);
        if (category && !category->empty() && map.count(*category)) return true;
    }
    return false;
}

// Reorders `candidates` (in place) so the preferred default-pick lands at
// front() -- alphabetical (already `scanFuzzyTexturePool`'s own sort order)
// for every ordinary slot, but for the two compositing types (1/8, see
// candidateCategoryTypes' doc comment) a bare or "skin_color" file is moved
// ahead of a narrower overlay like "face": a full-body base skin tone is a
// far more plausible stand-in for "the whole skin texture" than a small
// face-only overlay would be, even though neither is the real composited
// answer.
void preferBaseLayerCandidate(std::vector<std::filesystem::path>& candidates, uint32_t textureType,
                               const std::string& modelBasenameLower) {
    if (textureType != 1 && textureType != 8) return;
    std::stable_sort(candidates.begin(), candidates.end(),
                      [&](const std::filesystem::path& a, const std::filesystem::path& b) {
                          auto rank = [&](const std::filesystem::path& p) {
                              auto cat = classifyCandidateCategory(p, modelBasenameLower);
                              bool isBaseLayer = cat && (cat->empty() || *cat == "skin_color");
                              return isBaseLayer ? 0 : 1;
                          };
                          return rank(a) < rank(b);
                      });
}

std::optional<std::filesystem::path> claimSoleFuzzyTextureCandidate(FuzzyTexturePool& pool, uint32_t textureType,
                                                                      const std::string& modelBasenameLower,
                                                                      bool bareMeansSkinOnly) {
    std::optional<size_t> soleMatch;
    for (size_t i = 0; i < pool.files.size(); ++i) {
        if (!candidateAllowedForType(classifyCandidateCategory(pool.files[i], modelBasenameLower), textureType,
                                      bareMeansSkinOnly))
            continue;
        if (soleMatch) return std::nullopt;  // 2+ type-compatible candidates -- still ambiguous
        soleMatch = i;
    }
    if (!soleMatch) return std::nullopt;
    auto result = pool.files[*soleMatch];
    pool.files.erase(pool.files.begin() + *soleMatch);
    return result;
}

FuzzyTexturePool scanFuzzyTexturePool(const std::string& texturesDir, const std::string& modelPath) {
    FuzzyTexturePool pool;
    if (texturesDir.empty()) return pool;

    std::string modelBasename = lowercaseModelBasename(modelPath);
    if (modelBasename.empty()) return pool;

    // stem (lowercase) -> chosen path -- a directory holding both an
    // already-converted "<name>.png" and its source "<name>.blp" counts as
    // one real candidate, not two, and PNG wins (no decode needed).
    std::map<std::string, std::filesystem::path> byStem;
    for (const auto& entry : scanDirOrWarn(texturesDir, "textures directory")) {
        if (!entry.is_regular_file()) continue;
        const auto& path = entry.path();
        if (path.extension() != ".png" && path.extension() != ".blp") continue;
        std::string stem = path.stem().string();
        bool allDigits =
            !stem.empty() && std::all_of(stem.begin(), stem.end(),
                                          [](unsigned char c) { return std::isdigit(c) != 0; });
        if (allDigits) continue;  // exact-FileDataID path already covers these
        std::string stemLower = stem;
        std::transform(stemLower.begin(), stemLower.end(), stemLower.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (stemLower.rfind(modelBasename, 0) != 0) continue;  // starts with the model's basename
        auto [it, inserted] = byStem.try_emplace(stemLower, path);
        if (!inserted && path.extension() == ".png") it->second = path;  // PNG wins over BLP
    }
    for (auto& [stem, path] : byStem) pool.files.push_back(std::move(path));
    std::sort(pool.files.begin(), pool.files.end());
    return pool;
}

}  // namespace

std::filesystem::directory_iterator scanDirOrWarn(const std::string& dir, const char* purpose) {
    std::error_code statEc;
    auto st = std::filesystem::status(dir, statEc);
    if (statEc) {
        std::cerr << "husk: warning: " << purpose << " '" << dir << "': " << statEc.message() << "\n";
        return {};
    }
    if (st.type() == std::filesystem::file_type::not_found) {
        std::error_code linkEc;
        auto lst = std::filesystem::symlink_status(dir, linkEc);
        bool brokenSymlink = !linkEc && lst.type() == std::filesystem::file_type::symlink;
        std::cerr << "husk: warning: " << purpose << " '" << dir << "': "
                  << (brokenSymlink ? "broken symlink (target doesn't exist)" : "no such directory")
                  << "\n";
        return {};
    }
    if (st.type() != std::filesystem::file_type::directory) {
        std::cerr << "husk: warning: " << purpose << " '" << dir << "': not a directory\n";
        return {};
    }
    std::error_code iterEc;
    std::filesystem::directory_iterator it(dir, iterEc);
    if (iterEc) {
        std::cerr << "husk: warning: " << purpose << " '" << dir << "': " << iterEc.message() << "\n";
        return {};
    }
    return it;
}

BuiltMaterials buildMaterialsAndPrimitives(const std::vector<uint32_t>& triangleIndices,
                                            const std::vector<skin::Submesh>& submeshes,
                                            const std::vector<skin::Batch>& batches,
                                            const M2MaterialInputs& m2,
                                            const std::string& texturesDir,
                                            const std::string& modelPath,
                                            const std::string& texturesOutDir) {
    BuiltMaterials result;

    // Scanned once per skin/LOD, shared and depleted across every batch
    // below -- see scanFuzzyTexturePool's doc comment for why a fresh
    // per-batch scan would wrongly reuse the same real file across every
    // unresolved slot.
    FuzzyTexturePool fuzzyTexturePool = scanFuzzyTexturePool(texturesDir, modelPath);
    std::string modelBasenameLower = lowercaseModelBasename(modelPath);
    bool bareMeansSkinOnly = poolHasRecognizedCategory(fuzzyTexturePool.files, modelBasenameLower);

    // Ambiguous-candidate byte cache, keyed by path -- a real character
    // model can have dozens of hardcoded slots that are *all* ambiguous
    // against the *same* shared candidate pool (confirmed: 19 slots, 94
    // candidates each, on a real `bloodelffemale_hd.m2` export), and
    // without this every one of those slots independently re-reads and
    // re-decodes every candidate from disk -- 1,786 redundant BLP decodes
    // for that one real file, ~5.5 minutes of runtime for what should be
    // ~94. Read once, reused by every ambiguous slot that needs the same
    // file (see gltf_mesh.cpp's emitMaterial for the matching fix that
    // also keeps the *embedded* bytes from being duplicated once per
    // material in the final .glb, not just the read/decode cost here).
    std::map<std::filesystem::path, std::vector<uint8_t>> ambiguousCandidateCache;

    if (batches.empty()) {
        // A genuinely geometry-less .skin (real corpus
        // shape -- pure particle/ribbon VFX models, zero vertices at the M2
        // level, not just an empty batch table) has no triangles to put in
        // a primitive at all. Leave `result.primitives` empty rather than
        // manufacturing one with empty `indices` -- glTF has no valid
        // "primitive with zero indices" representation, so the caller
        // (cmd_export.cpp) skips adding a mesh node for this LOD tier
        // entirely when `result.primitives` comes back empty.
        if (triangleIndices.empty()) {
            return result;
        }
        gltf::Primitive prim;
        prim.indices = triangleIndices;
        result.primitives.push_back(std::move(prim));
        return result;
    }

    std::set<uint16_t> skinSectionIds;
    for (size_t bi = 0; bi < batches.size(); ++bi) {
        const auto& b = batches[bi];
        if (b.skinSectionIndex >= submeshes.size()) {
            throw std::runtime_error("batch " + std::to_string(bi) + "'s skinSectionIndex (" +
                                      std::to_string(b.skinSectionIndex) +
                                      ") is out of range for " + std::to_string(submeshes.size()) +
                                      " submeshes");
        }
        const auto& sm = submeshes[b.skinSectionIndex];
        skinSectionIds.insert(sm.skinSectionId);
        if (b.textureCount > 1) {
            ++result.multiTextureBatchCount;
        }
        if (static_cast<size_t>(sm.indexStart) + sm.indexCount > triangleIndices.size()) {
            throw std::runtime_error(
                "submesh " + std::to_string(b.skinSectionIndex) +
                "'s index range runs past the end of the resolved triangle-index buffer -- "
                "corrupted .skin?");
        }

        // The minority case: a submesh with zero indices
        // alongside sibling submeshes that have real geometry (mixed real+
        // empty geosets in one .skin) -- the same "don't manufacture
        // a primitive glTF can't represent" rule applies per-primitive:
        // skip just this batch (no primitive, no material) rather than
        // emitting a zero-indices primitive that would fail writeGlbMulti's
        // hard check.
        if (sm.indexCount == 0) {
            continue;
        }

        gltf::Primitive prim;
        prim.indices.assign(triangleIndices.begin() + sm.indexStart,
                             triangleIndices.begin() + sm.indexStart + sm.indexCount);
        prim.skinSectionId = sm.skinSectionId;

        if (b.materialIndex >= m2.materials.size()) {
            throw std::runtime_error("batch " + std::to_string(bi) + "'s materialIndex (" +
                                      std::to_string(b.materialIndex) + ") is out of range for " +
                                      std::to_string(m2.materials.size()) + " materials");
        }
        const auto& mat = m2.materials[b.materialIndex];

        gltf::Material gm;
        gm.alphaMode = alphaModeForBlend(mat.blendMode);
        gm.doubleSided = (mat.flags & kMaterialTwoSidedFlag) != 0;
        gm.unlit = (mat.flags & kMaterialUnlitFlag) != 0;
        gm.name = "batch" + std::to_string(bi) + "_mat" + std::to_string(b.materialIndex);

        // Vertex-color tint + combined alpha/texture-weight fade (static
        // approximation -- see m2::Color/TextureWeight). colorIndex is
        // genuinely optional (0xFFFF/"none" is common and expected);
        // textureWeightComboIndex is not documented as nullable and every
        // real batch this was tested against has a valid one, so it's
        // resolved unconditionally and bounds-checked like any other index.
        if (b.colorIndex != 0xFFFF) {
            if (b.colorIndex >= m2.colors.size()) {
                throw std::runtime_error("batch " + std::to_string(bi) + "'s colorIndex (" +
                                          std::to_string(b.colorIndex) + ") is out of range for " +
                                          std::to_string(m2.colors.size()) + " colors");
            }
            const auto& color = m2.colors[b.colorIndex];
            if (color.color) {
                gm.baseColorFactor[0] = color.color->x;
                gm.baseColorFactor[1] = color.color->y;
                gm.baseColorFactor[2] = color.color->z;
            }
            if (color.alpha) {
                gm.baseColorFactor[3] *= *color.alpha;
            }
            if (color.colorAnimated || color.alphaAnimated) {
                ++result.animatedTintOrFadeBatchCount;
                // Full curve dump -- diagnostic-only
                // extras, see gltf::Material::tintAnimation/
                // alphaFadeAnimation's doc comments. `m2.blob` is only
                // unset for a hypothetical caller that never populated it
                // (none exists today, see M2MaterialInputs's doc comment) --
                // best-effort like additionalTextureLayers/textureTransform
                // above, not required for a usable export.
                if (m2.blob) {
                    if (color.colorAnimated) {
                        gm.tintAnimation = resolveAnimatedColorCurve(*m2.blob, color.colorTrackOffset,
                                                                       m2.sequenceCount);
                    }
                    if (color.alphaAnimated) {
                        gm.alphaFadeAnimation = resolveAnimatedFixed16Curve(
                            *m2.blob, color.alphaTrackOffset, m2.sequenceCount);
                    }
                }
            }
        }
        // Like textureCoordCombos above, treat a completely empty table as
        // "this model doesn't use this feature" rather than an error --
        // unlike textureCoordCombos there's no documented version cutoff
        // for this one, but the same defensive reasoning applies: a
        // model-wide absence of transparency-weight data shouldn't turn
        // into every single batch failing to export.
        if (!m2.textureWeightCombos.empty()) {
            if (b.textureWeightComboIndex >= m2.textureWeightCombos.size()) {
                throw std::runtime_error(
                    "batch " + std::to_string(bi) + "'s textureWeightComboIndex (" +
                    std::to_string(b.textureWeightComboIndex) + ") is out of range for " +
                    std::to_string(m2.textureWeightCombos.size()) + " textureWeightCombos entries");
            }
            uint16_t weightIndex = m2.textureWeightCombos[b.textureWeightComboIndex];
            if (weightIndex >= m2.textureWeights.size()) {
                throw std::runtime_error(
                    "batch " + std::to_string(bi) + "'s texture weight (index " +
                    std::to_string(weightIndex) + " via textureWeightCombos[" +
                    std::to_string(b.textureWeightComboIndex) + "]) is out of range for " +
                    std::to_string(m2.textureWeights.size()) + " textureWeights entries");
            }
            const auto& weight = m2.textureWeights[weightIndex];
            if (weight.weight) {
                gm.baseColorFactor[3] *= *weight.weight;
            }
            if (weight.weightAnimated) {
                ++result.animatedTintOrFadeBatchCount;
                if (m2.blob) {
                    gm.weightFadeAnimation = resolveAnimatedFixed16Curve(
                        *m2.blob, weight.weightTrackOffset, m2.sequenceCount);
                }
            }
        }

        if (b.textureCount > 0) {
            if (b.textureComboIndex >= m2.textureCombos.size()) {
                throw std::runtime_error(
                    "batch " + std::to_string(bi) + "'s textureComboIndex (" +
                    std::to_string(b.textureComboIndex) + ") is out of range for " +
                    std::to_string(m2.textureCombos.size()) + " textureCombos entries");
            }
            uint16_t textureIndex = m2.textureCombos[b.textureComboIndex];
            if (textureIndex >= m2.textures.size()) {
                throw std::runtime_error(
                    "batch " + std::to_string(bi) + "'s texture (index " +
                    std::to_string(textureIndex) + " via textureCombos[" +
                    std::to_string(b.textureComboIndex) + "]) is out of range for " +
                    std::to_string(m2.textures.size()) + " textures");
            }
            gm.name += "_tex" + std::to_string(textureIndex);

            // M2Texture::type -- see
            // gltf::Material::textureType's doc comment for why this is a
            // real "husk can't resolve this locally" signal for any nonzero
            // value, not just missing PNG data.
            gm.textureType = m2.textures[textureIndex].type;
            // A real semantic name (e.g. "_skin", "_char_hair") instead of
            // a bare "_tex<N>" whenever the type is known -- per Luna's own
            // "clearly named slots based on the texture they utilize" ask.
            // Type 0 (a real embedded/FileDataID-resolvable texture) gets
            // no suffix here; its own filename/FileDataID below already
            // says more than the generic type name would.
            if (const char* typeName = m2::textureTypeName(gm.textureType)) {
                gm.name += std::string("_") + typeName;
            }

            // Second UV set (roadmap "Second UV set" feature): only
            // meaningful pre-Cataclysm, see M2MaterialInputs::
            // textureCoordCombos's doc comment -- an empty table (every
            // modern file) always means UV set 0.
            if (!m2.textureCoordCombos.empty()) {
                if (b.textureCoordComboIndex >= m2.textureCoordCombos.size()) {
                    throw std::runtime_error(
                        "batch " + std::to_string(bi) + "'s textureCoordComboIndex (" +
                        std::to_string(b.textureCoordComboIndex) + ") is out of range for " +
                        std::to_string(m2.textureCoordCombos.size()) +
                        " textureCoordCombos entries");
                }
                uint16_t mapping = m2.textureCoordCombos[b.textureCoordComboIndex];
                // 0xFFFF (-1) is environment mapping, which has no glTF
                // equivalent -- fall back to UV set 0 rather than guessing.
                if (mapping == 1) {
                    gm.baseColorTexCoord = 1;
                }
            }

            uint32_t fdid = (m2.textureFileDataIds && textureIndex < m2.textureFileDataIds->size())
                                 ? (*m2.textureFileDataIds)[textureIndex]
                                 : 0;
            const std::string& embeddedFilename = m2.textures[textureIndex].filename;
            std::string embeddedStem;
            std::optional<std::vector<uint8_t>> embeddedBytes;
            if (!embeddedFilename.empty() && !texturesDir.empty()) {
                embeddedStem = std::filesystem::path(embeddedFilename).stem().string();
                embeddedBytes = resolveTextureBytes(std::filesystem::path(texturesDir) / embeddedStem,
                                                     texturesDir, texturesOutDir);
            }

            // Priority order: every *deterministic* signal (never a guess,
            // never touches the shared fuzzy pool) before the one heuristic
            // signal (a real-name-only extraction has no other way to
            // identify a texture). `fdid`, when resolved, is recorded in
            // the material name and `gm.baseColorTextureFileDataId`
            // regardless of which path below actually supplies the
            // embedded bytes.
            //
            // The fuzzy pool specifically is deliberately tried *last*, not
            // first: it's a real, if bounded, guess, and the pool is shared
            // and depleted across every batch in this call -- letting a
            // slot draw from it before checking whether it already has a
            // *working*, deterministic match would let an early,
            // genuinely-hardcoded slot claim a real file that actually
            // belongs (by a later-processed slot's own resolvable
            // FileDataID) to someone else, silently mismatching *both*
            // slots.
            if (fdid != 0) {
                gm.name += "_fdid" + std::to_string(fdid);
                gm.baseColorTextureFileDataId = fdid;
            }

            bool embedded = false;
            if (embeddedBytes) {
                // A real embedded path (wowdev.wiki M2#Textures, older/
                // classic-era files per m2::Texture's own doc comment).
                // Not a guess: `filename` is real data straight from this
                // M2, so the same basename (BLP or PNG) is an exact lookup
                // -- the single most precise signal available, tried first
                // regardless of whether a FileDataID also resolved.
                gm.name += "_" + embeddedStem;
                gm.baseColorImagePng = std::move(*embeddedBytes);
                gm.baseColorImageName = embeddedStem;
                embedded = true;
            }
            if (!embedded && fdid != 0 && !texturesDir.empty()) {
                // Deterministic whenever the file is actually present --
                // still the right answer for a real extraction that *does*
                // use the FileDataID-named convention (this project's own
                // test fixtures and the common "casc-tool-style"
                // "<FileDataID>.{png,blp}" layout both do).
                if (auto bytes = resolveTextureBytes(std::filesystem::path(texturesDir) /
                                                          std::to_string(fdid),
                                                      texturesDir, texturesOutDir)) {
                    gm.baseColorImagePng = std::move(*bytes);
                    gm.baseColorImageName = std::to_string(fdid);
                    embedded = true;
                }
            }
            if (!embedded) {
                // Last resort, for whatever's left unresolved after both
                // deterministic paths above -- a genuinely hardcoded slot
                // (fdid == 0, as before this change), *or* a slot whose
                // FileDataID resolved but no "<fdid>.{png,blp}" actually
                // exists in texturesDir (new: previously this case silently
                // embedded nothing, even when a real, descriptively-named
                // file for it was sitting right there unclaimed).
                if (auto fuzzy = claimSoleFuzzyTextureCandidate(fuzzyTexturePool, gm.textureType, modelBasenameLower,
                                                                  bareMeansSkinOnly)) {
                    if (auto bytes = readTextureFileBytes(*fuzzy, texturesDir, texturesOutDir)) {
                        gm.name += "_" + fuzzy->stem().string();
                        gm.baseColorImagePng = std::move(*bytes);
                        gm.baseColorImageName = fuzzy->stem().string();
                        result.fuzzyMatches.push_back({gm.name, fuzzy->filename().string(), fdid});
                    }
                } else {
                    // Not claimed (removed) from the shared pool -- every
                    // other ambiguous slot is equally uninformed about
                    // *this* slot's own real candidates and deserves its
                    // own independently-filtered view, not whichever's left
                    // after an earlier slot's pick. Narrowed first to only
                    // the candidates whose own filename category is
                    // actually compatible with this slot's textureType
                    // (candidateAllowedForType's doc comment) -- a real,
                    // grounded exclusion, not a guess about which one is
                    // correct.
                    std::vector<std::filesystem::path> matching;
                    for (const auto& candidatePath : fuzzyTexturePool.files) {
                        if (candidateAllowedForType(classifyCandidateCategory(candidatePath, modelBasenameLower),
                                                     gm.textureType, bareMeansSkinOnly)) {
                            matching.push_back(candidatePath);
                        }
                    }
                    if (matching.size() > 1) {
                        // Genuinely ambiguous: 2+ type-compatible
                        // candidates, no way to tell which one this slot
                        // actually wants. Embed every one of them
                        // (gltf_mesh.hpp's AlternateTextureCandidate doc
                        // comment has the full rationale), same "export
                        // everything, let the client filter" treatment
                        // mutually-exclusive geosets already get --
                        // reordered first so the most plausible default
                        // (preferBaseLayerCandidate's doc comment) lands at
                        // front(), the one that becomes the wired default.
                        preferBaseLayerCandidate(matching, gm.textureType, modelBasenameLower);
                        std::vector<std::string> allFileNames;
                        for (const auto& candidatePath : matching) {
                            auto cached = ambiguousCandidateCache.find(candidatePath);
                            if (cached == ambiguousCandidateCache.end()) {
                                auto bytes = readTextureFileBytes(candidatePath, texturesDir, texturesOutDir);
                                if (!bytes) continue;
                                cached = ambiguousCandidateCache.emplace(candidatePath, std::move(*bytes)).first;
                            }
                            gltf::Material::AlternateTextureCandidate cand;
                            cand.filename = candidatePath.filename().string();
                            auto category = classifyCandidateCategory(candidatePath, modelBasenameLower);
                            cand.category = category.value_or(std::string());
                            cand.imagePng = cached->second;
                            allFileNames.push_back(cand.filename);
                            gm.alternateTextureCandidates.push_back(std::move(cand));
                        }
                        if (!gm.alternateTextureCandidates.empty()) {
                            const auto& chosen = gm.alternateTextureCandidates.front();
                            gm.name += "_" + std::filesystem::path(chosen.filename).stem().string();
                            gm.baseColorImagePng = chosen.imagePng;
                            gm.baseColorImageName = std::filesystem::path(chosen.filename).stem().string();
                            result.ambiguousMatches.push_back(
                                {gm.name, chosen.filename, std::move(allFileNames), fdid});
                        }
                    }
                }
            }

            // Additional texture layers (textureCount > 1): per wowdev.wiki
            // M2/.skin#Texture_units, textureComboIndex is a *base* index --
            // layer i's real combo index is textureComboIndex + i. Resolved
            // best-effort, not with the same "foreign data must fit its own
            // claims" strictness as the primary texture above: this is
            // supplementary metadata, not required for a usable export, so
            // an out-of-range layer is skipped rather than failing the
            // whole batch.
            for (uint16_t layer = 1; layer < b.textureCount; ++layer) {
                size_t comboIdx = static_cast<size_t>(b.textureComboIndex) + layer;
                if (comboIdx >= m2.textureCombos.size()) break;
                uint16_t layerTextureIndex = m2.textureCombos[comboIdx];
                if (layerTextureIndex >= m2.textures.size()) continue;

                gltf::Material::AdditionalTextureLayer al;
                if (!m2.textureCoordCombos.empty()) {
                    size_t coordComboIdx = static_cast<size_t>(b.textureCoordComboIndex) + layer;
                    if (coordComboIdx < m2.textureCoordCombos.size() &&
                        m2.textureCoordCombos[coordComboIdx] == 1) {
                        al.texCoord = 1;
                    }
                }
                if (m2.textureFileDataIds && layerTextureIndex < m2.textureFileDataIds->size()) {
                    al.fileDataId = (*m2.textureFileDataIds)[layerTextureIndex];
                    if (al.fileDataId != 0 && !texturesDir.empty()) {
                        if (auto bytes = resolveTextureBytes(std::filesystem::path(texturesDir) /
                                                                  std::to_string(al.fileDataId),
                                                              texturesDir, texturesOutDir)) {
                            al.imagePng = std::move(*bytes);
                        }
                    }
                }
                gm.additionalTextureLayers.push_back(std::move(al));
            }
        }

        // UV scroll/rotate/scale animation: resolved the same "sentinel
        // means none" way colorIndex is, then exposed as inert extras --
        // see m2::TextureTransform's doc comment for why this never becomes
        // a real KHR_texture_transform on the render. Best-effort like the
        // additional-texture-layers loop just above (an out-of-range index
        // is skipped, not a failure) -- this is supplementary metadata, not
        // required for a usable export.
        if (b.textureTransformComboIndex != 0xFFFF &&
            b.textureTransformComboIndex < m2.textureTransformCombos.size()) {
            uint16_t transformIndex = m2.textureTransformCombos[b.textureTransformComboIndex];
            if (transformIndex < m2.textureTransforms.size()) {
                const auto& xf = m2.textureTransforms[transformIndex];
                gltf::Material::TextureTransform gxf;
                gxf.constant =
                    !xf.translationAnimated && !xf.rotationAnimated && !xf.scalingAnimated;
                if (xf.translation) {
                    gxf.translation = {xf.translation->x, xf.translation->y, xf.translation->z};
                }
                if (xf.rotation) {
                    gxf.rotation[0] = xf.rotation->x;
                    gxf.rotation[1] = xf.rotation->y;
                    gxf.rotation[2] = xf.rotation->z;
                    gxf.rotation[3] = xf.rotation->w;
                }
                if (xf.scaling) {
                    gxf.scaling = {xf.scaling->x, xf.scaling->y, xf.scaling->z};
                }
                gm.textureTransform = gxf;
                ++result.textureTransformBatchCount;
            }
        }

        prim.materialIndex = static_cast<int>(result.materials.size());
        result.materials.push_back(std::move(gm));
        result.primitives.push_back(std::move(prim));
    }

    result.distinctSkinSectionIds.assign(skinSectionIds.begin(), skinSectionIds.end());

    // Whatever's left in the pool never got claimed -- either nothing
    // needed it (fine, silent) or 2+ files shared the model's basename and
    // husk couldn't tell which unresolved slot(s) they belonged to. Reported
    // once per skin/LOD, not per batch, so their existence is visible
    // without being noisy.
    if (fuzzyTexturePool.files.size() > 1) {
        std::cout << "husk: note: " << fuzzyTexturePool.files.size()
                  << " texture file(s) in '" << texturesDir
                  << "' share this model's basename but husk can't tell which hardcoded texture "
                     "slot each belongs to -- none were embedded\n";
    }

    return result;
}

}  // namespace husk::commands
