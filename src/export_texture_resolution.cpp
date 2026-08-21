#include "export_texture_resolution.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>

#include "blp.hpp"
#include "export_materials.hpp"
#include "export_transform.hpp"
#include "m2_animation.hpp"

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
// "skin_color"/"face"/"body_jewelry"/"bracelets" all stay valid candidates
// for those two types -- see `orderCandidatesForDefault` for how the
// default is actually chosen among them (real decoded pixel area, not a
// category-name preference: a same-category token can span genuinely
// different kinds of asset, see that function's own doc comment).
//
// "body_jewelry"/"bracelets" are mapped to 1/8 (skin/skin_extra), not 20
// (char_jewelry) alongside "jewelry_color" -- an earlier version of this
// table did put them under type 20, on the assumption that anything
// English-named "jewelry" belongs to the same slot; real, direct
// verification (`LUNA_FINDINGS.md`, Blender inspection of a real
// `bloodelffemale_hd.m2` export) first found only `jewelry_color_3613861`/
// `_3613862` are actually correct for its one `char_jewelry` material, then
// explained *why*: `body_jewelry`/`bracelets` are flat texture overlays
// meant to be composited onto the skin texture itself (same family as
// `skin_color`/`face`, no UV map of their own), while `jewelry_color`
// textures a genuinely separate 3D jewelry mesh with its own UV map --
// different kind of thing entirely, not just a different customization
// category of the same kind.
const std::unordered_map<std::string, std::vector<uint32_t>>& candidateCategoryTypes() {
    static const std::unordered_map<std::string, std::vector<uint32_t>> kMap = {
        {"skin_color", {1, 8}},
        {"face", {1, 8}},
        {"body_jewelry", {1, 8}},
        {"bracelets", {1, 8}},
        {"hair_color", {6, 22}},
        {"hair_style", {6, 22}},
        {"facial_hair", {7}},
        {"eye_color", {19}},
        {"jewelry_color", {20}},
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
// A bare "<model>_<FileDataID>" candidate is deliberately treated as just
// another unrecognized category (same as a truly unclassifiable token),
// NOT specially trusted as "the" skin/skin_extra layer -- an earlier
// version of this function did assume that, and real evidence proved it
// wrong: on `bloodelffemale_hd`, the bare `..._3255415.blp` file that kept
// winning as the alphabetically-first default turned out (viewed via
// `husk-blp`) to be a tiny mostly-transparent sparkle/glint icon, nothing
// like a skin texture, while the real full-body atlas was sitting right
// there under the *recognized* `skin_color` category the whole time. Bare
// files aren't reliably anything in particular -- see
// `filterCandidatesForType`'s doc comment for how this is actually kept
// out of the way now, by falling back to it only when nothing recognized
// exists at all, rather than by guessing what it is upfront.
bool candidateAllowedForType(const std::optional<std::string>& category, uint32_t textureType) {
    if (!category || category->empty()) return true;
    const auto& map = candidateCategoryTypes();
    auto it = map.find(*category);
    if (it == map.end()) return true;
    return std::find(it->second.begin(), it->second.end(), textureType) != it->second.end();
}

// A "_glow_<color>" (or "_glow") suffix is a real, common WoW texture
// naming convention -- a highlight/emissive layer meant to be rendered in
// its *own* separate additive-blend batch, never a stand-in for a normal
// opaque/alpha batch's base diffuse. Confirmed against real corpus bytes,
// not guessed, two ways: item/objectcomponents/weapon/
// mace_1h_raidmidnight_d_01_glow_blue.blp decodes to a 2048x1024
// near-solid-black image with a small blue highlight region, while the
// model's own real base diffuse, _blue.blp, is a normal 512x256 detailed
// texture; creature/tripod2/tripod2_blue_glow.blp decodes to a mostly-white
// masked emissive image, visually unrelated to tripod2_blue.blp's real
// full-color diffuse past sharing a basename prefix. Checked as a whole
// underscore-delimited token (not a bare substring match) so a hypothetical
// "afterglow"-style name isn't misclassified.
bool isGlowVariantCandidate(const std::filesystem::path& path, const std::string& modelBasenameLower) {
    auto category = classifyCandidateCategory(path, modelBasenameLower);
    if (!category) return false;
    std::istringstream tokens(*category);
    std::string token;
    while (std::getline(tokens, token, '_')) {
        if (token == "glow") return true;
    }
    return false;
}

// Scans `texturesDir` for real (.png/.blp) files whose stem starts with
// `basename` (case-insensitive) -- the actual directory walk, factored out
// so scanFuzzyTexturePool can retry it against a second, derived basename
// (the "_sdr" fallback below) without duplicating the scan/dedup logic.
FuzzyTexturePool scanFuzzyTexturePoolForBasename(const std::string& texturesDir, const std::string& basename) {
    FuzzyTexturePool pool;
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
        if (stemLower.rfind(basename, 0) != 0) continue;  // starts with the given basename
        auto [it, inserted] = byStem.try_emplace(stemLower, path);
        if (!inserted && path.extension() == ".png") it->second = path;  // PNG wins over BLP
    }
    for (auto& [stem, path] : byStem) pool.files.push_back(std::move(path));
    std::sort(pool.files.begin(), pool.files.end());
    return pool;
}

// Real, confirmed-by-bytes naming convention (not a guess): a "_sdr"
// stand-in character model (a drastically lower-poly, self-contained-
// animation variant -- see CLAUDE_HISTORY.md's 2026-08-09 entry for the
// full comparison) shares its real texture files with its non-"_sdr"
// counterpart. E.g. "darkirondwarfmale_sdr.m2"'s own hardcoded texture
// slots can only ever resolve against files named
// "darkirondwarfmale_<suffix>.blp" -- real files, sitting in the same
// directory -- never "darkirondwarfmale_sdr_<suffix>.blp", which doesn't
// exist anywhere in the corpus. Without this, every "_sdr" file's
// same-basename pool is unconditionally empty and every hardcoded slot
// renders untextured.
constexpr std::string_view kSdrSuffix = "_sdr";

// Real, corpus-verified race/gender suffix codes (not a guess, not from
// any wowdev.wiki table -- derived by frequency-counting every real
// "..._<code>[_]<m|f>.m2" filename across item/objectcomponents/ in a
// real 130k-file local extraction; only codes with hundreds of real
// occurrences kept, see TODO/KNOWLEDGE_BASE_DESIGN.md's "local fallback"
// section for the exact counts). Both the compact ("_bem") and
// underscore-separated ("_be_m") forms are real and both appear widely
// across different eras of content, so both are tried for every code
// rather than special-casing which convention a given code uses.
constexpr std::array<std::string_view, 20> kRaceCodes = {
    "be", "dr", "dt", "dw", "gn", "go", "hr", "hu", "kt", "mg",
    "nb", "ni", "or", "pa", "sc", "ta", "tr", "vu", "wo", "za",
};

// Strips a trailing race/gender suffix from a lowercase model basename,
// e.g. "helm_leather_pvpdruid_b_02_scm" -> "helm_leather_pvpdruid_b_02"
// -- the real naming convention behind husk's own item/objectcomponents/
// race-variant `.m2` files (22+ real files sharing one base shape,
// confirmed directly: TODO/KNOWLEDGE_BASE_DESIGN.md). Returns nullopt
// when no known suffix matches -- never a partial/ambiguous strip.
std::optional<std::string> stripRaceGenderSuffix(const std::string& basenameLower) {
    for (std::string_view code : kRaceCodes) {
        for (char gender : {'m', 'f'}) {
            for (const std::string& suffix :
                 {std::string("_") + std::string(code) + gender,
                  std::string("_") + std::string(code) + "_" + gender}) {
                if (basenameLower.size() > suffix.size() &&
                    basenameLower.compare(basenameLower.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    return basenameLower.substr(0, basenameLower.size() - suffix.size());
                }
            }
        }
    }
    return std::nullopt;
}

}  // namespace

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

gltf::Material::AlphaMode alphaModeForBlend(uint16_t blendMode) {
    switch (blendMode) {
        case 0: return gltf::Material::AlphaMode::Opaque;
        case 1: return gltf::Material::AlphaMode::Mask;
        default: return gltf::Material::AlphaMode::Blend;
    }
}

std::vector<gltf::Material::AnimatedColorCurve> resolveAnimatedColorCurve(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, size_t sequenceCount) {
    return resolveAnimatedCurveGeneric<gltf::Material::AnimatedColorCurve>(
        blob, trackOffset, sequenceCount, m2::resolveVec3TrackSequence, m2::resolveVec3GlobalSequenceTrack,
        [](const m2::Vec3& v) { return gltf::Vec3{v.x, v.y, v.z}; });
}

std::vector<gltf::Material::AnimatedQuatCurve> resolveAnimatedRawQuatCurve(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, size_t sequenceCount) {
    return resolveAnimatedCurveGeneric<gltf::Material::AnimatedQuatCurve>(
        blob, trackOffset, sequenceCount, m2::resolveRawQuatTrackSequence,
        m2::resolveRawQuatGlobalSequenceTrack,
        [](const m2::Quat& q) { return std::array<float, 4>{q.x, q.y, q.z, q.w}; });
}

std::vector<gltf::Material::AnimatedScalarCurve> resolveAnimatedFixed16Curve(
    const std::vector<uint8_t>& blob, uint32_t trackOffset, size_t sequenceCount) {
    auto resolveSeq = [](const std::vector<uint8_t>& b, uint32_t off, uint32_t si,
                          const std::vector<uint8_t>* ext) {
        return m2::resolveRawIntTrackSequence(b, off, si, /*elementSize=*/2, ext);
    };
    auto resolveGlobal = [](const std::vector<uint8_t>& b, uint32_t off, const std::vector<uint8_t>* ext) {
        return m2::resolveRawIntGlobalSequenceTrack(b, off, /*elementSize=*/2, ext);
    };
    return resolveAnimatedCurveGeneric<gltf::Material::AnimatedScalarCurve>(
        blob, trackOffset, sequenceCount, resolveSeq, resolveGlobal,
        [](uint32_t bits) { return decodeFixed16(bits); });
}

std::string lowercaseModelBasename(const std::string& modelPath) {
    std::string basename = std::filesystem::path(modelPath).stem().string();
    std::transform(basename.begin(), basename.end(), basename.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return basename;
}

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

std::optional<uint32_t> fuzzyCandidateFileDataId(const std::filesystem::path& path,
                                                   const std::string& modelBasenameLower) {
    std::string stem = path.stem().string();
    std::transform(stem.begin(), stem.end(), stem.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (stem.rfind(modelBasenameLower, 0) != 0) return std::nullopt;
    std::string rest = stem.substr(modelBasenameLower.size());
    if (!rest.empty() && rest.front() == '_') rest.erase(0, 1);
    if (rest.empty()) return std::nullopt;

    auto isAllDigits = [](const std::string& s) {
        return !s.empty() &&
               std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
    };
    std::string digits;
    if (isAllDigits(rest)) {
        digits = rest;
    } else {
        auto lastUnderscore = rest.find_last_of('_');
        if (lastUnderscore == std::string::npos) return std::nullopt;
        std::string tail = rest.substr(lastUnderscore + 1);
        if (!isAllDigits(tail)) return std::nullopt;
        digits = tail;
    }
    // A real FileDataID fits uint32_t; a longer digit run can't be one --
    // bail rather than let std::stoul throw/overflow on pathological input.
    if (digits.size() > 10) return std::nullopt;
    return static_cast<uint32_t>(std::stoul(digits));
}

std::vector<std::filesystem::path> filterCandidatesForType(const std::vector<std::filesystem::path>& files,
                                                             uint32_t textureType,
                                                             const std::string& modelBasenameLower) {
    std::vector<std::filesystem::path> recognized;
    std::vector<std::filesystem::path> fallback;
    const auto& map = candidateCategoryTypes();
    for (const auto& p : files) {
        auto category = classifyCandidateCategory(p, modelBasenameLower);
        if (!candidateAllowedForType(category, textureType)) continue;
        bool isRecognized = category && !category->empty() && map.count(*category) != 0;
        (isRecognized ? recognized : fallback).push_back(p);
    }
    return recognized.empty() ? fallback : recognized;
}

std::pair<uint32_t, uint32_t> pngDimensions(const std::vector<uint8_t>& png) {
    if (png.size() < 24) return {0, 0};
    auto be32 = [&](size_t off) {
        return (static_cast<uint32_t>(png[off]) << 24) | (static_cast<uint32_t>(png[off + 1]) << 16) |
               (static_cast<uint32_t>(png[off + 2]) << 8) | static_cast<uint32_t>(png[off + 3]);
    };
    return {be32(16), be32(20)};
}

// Reorders `candidates` (in place, already filterCandidatesForType's
// output) so the preferred default-pick lands at front(), by four real,
// measured/verified signals, in order:
//
// 0. `preferGlow` (the caller's own batch is itself additive, `blendMode >
//    2` -- the same threshold `gltf::Material::blendMode`'s own doc
//    comment already uses for "no core-glTF alphaMode equivalent"): a
//    "glow" candidate (isGlowVariantCandidate) is preferred, not avoided.
//    Real bug this closes: `creature/tripod2.m2` has two ambiguous batches
//    sharing one same-basename candidate pool (tripod2_{blue,green,purple,
//    red}.blp + their _glow counterparts) -- one batch is the normal
//    opaque base skin (blendMode 0), the other is a separate additive-blend
//    glow layer (blendMode 4, a different M2Texture replaceable-type slot
//    with no filename/FileDataID of its own to disambiguate by). Before
//    this signal, both batches independently ranked the same non-glow
//    candidate first (signal 1 below, unconditionally avoiding "glow"),
//    so the additive batch rendered with a flat base-color texture instead
//    of its own real masked emissive image -- confirmed by actually
//    decoding both files and looking (see isGlowVariantCandidate's doc
//    comment).
// 1. Not a "glow" variant (isGlowVariantCandidate's own doc comment),
//    *unless* `preferGlow` inverted this above -- checked before pixel
//    area specifically because area alone picks the glow variant, the
//    real (opaque-batch) bug this signal originally fixed.
// 2. Decoded pixel area, largest first. Not a category-name heuristic --
//    real evidence against `bloodelffemale_hd` found the *same* recognized
//    category can span genuinely different kinds of asset: its own
//    "skin_color" token covers both several real 1024x512 full-body
//    atlases (matched skin-tone color variants of one design) *and*
//    several unrelated 256x128 underwear-strap decal overlays. A small
//    accent/decal texture is essentially never the right stand-in for "the
//    whole slot" when a large, atlas-shaped candidate exists in the same
//    set -- pixel count is a real, measured fact about the file, not a
//    guess about what it depicts.
// 2. Among same-area candidates, "skin_color" still wins -- confirmed
//    directly: `body_jewelry_3602029` (a real necklace-overlay texture,
//    correctly a type 1/8 candidate per candidateCategoryTypes' own doc
//    comment) happens to be the *same* 1024x512 resolution as the real
//    base skin atlas, so pixel area alone can't tell them apart, but
//    Luna's own direct explanation of the asset roles can: "skin_color"
//    ("the 'base' skin color that gets rendered under armors... the whole
//    character + face + face jewelry") is specifically the one meant to
//    stand alone as a complete look, while `body_jewelry`/`bracelets`/
//    `face` are overlays layered *on top of* it -- never the right
//    default by themselves, whatever their resolution.
//
// `readTextureFileBytes` result is cached in `byteCache` -- the *same*
// `ambiguousCandidateCache` map `buildMaterialsAndPrimitives` already
// shares across every ambiguous batch for the actual embed step, passed
// in by reference rather than a fresh local cache. This isn't just a
// convenience: a real character model can have dozens of batches sharing
// one candidate pool (the exact shape `ambiguousCandidateCache`'s own doc
// comment describes), and each one calls this function -- a fresh local
// cache would mean re-reading and re-decoding every candidate's `.blp`
// once per batch just to sort it, the same "1786 redundant decodes"
// regression this project already found and fixed once before with a
// different feature (finding #6). Sharing the cache means each candidate
// is decoded at most once for the *whole* export, not once per batch.
void orderCandidatesForDefault(std::vector<std::filesystem::path>& candidates, const std::string& texturesDir,
                                const std::string& texturesOutDir, const std::string& modelBasenameLower,
                                std::map<std::filesystem::path, std::vector<uint8_t>>& byteCache,
                                bool preferGlow) {
    auto areaOf = [&](const std::filesystem::path& p) -> uint64_t {
        auto cached = byteCache.find(p);
        if (cached == byteCache.end()) {
            auto bytes = readTextureFileBytes(p, texturesDir, texturesOutDir);
            if (!bytes) return 0;
            cached = byteCache.emplace(p, std::move(*bytes)).first;
        }
        auto [w, h] = pngDimensions(cached->second);
        return static_cast<uint64_t>(w) * h;
    };
    std::stable_sort(candidates.begin(), candidates.end(),
                      [&](const std::filesystem::path& a, const std::filesystem::path& b) {
                          bool glowA = isGlowVariantCandidate(a, modelBasenameLower);
                          bool glowB = isGlowVariantCandidate(b, modelBasenameLower);
                          if (glowA != glowB) return preferGlow ? glowA : !glowA;
                          uint64_t areaA = areaOf(a), areaB = areaOf(b);
                          if (areaA != areaB) return areaA > areaB;
                          auto isBaseLayer = [&](const std::filesystem::path& p) {
                              auto cat = classifyCandidateCategory(p, modelBasenameLower);
                              return cat && *cat == "skin_color";
                          };
                          return isBaseLayer(a) && !isBaseLayer(b);
                      });
}

std::optional<std::filesystem::path> claimSoleFuzzyTextureCandidate(FuzzyTexturePool& pool, uint32_t textureType,
                                                                      const std::string& modelBasenameLower) {
    auto matching = filterCandidatesForType(pool.files, textureType, modelBasenameLower);
    if (matching.size() != 1) return std::nullopt;  // 0 or 2+ -- nothing to unambiguously claim
    auto result = matching.front();
    pool.files.erase(std::find(pool.files.begin(), pool.files.end(), result));
    return result;
}

FuzzyTexturePool scanFuzzyTexturePool(const std::string& texturesDir, const std::string& modelPath) {
    if (texturesDir.empty()) return {};

    std::string modelBasename = lowercaseModelBasename(modelPath);
    if (modelBasename.empty()) return {};

    FuzzyTexturePool pool = scanFuzzyTexturePoolForBasename(texturesDir, modelBasename);
    if (pool.files.empty() && modelBasename.size() > kSdrSuffix.size() &&
        modelBasename.compare(modelBasename.size() - kSdrSuffix.size(), kSdrSuffix.size(), kSdrSuffix) == 0) {
        std::string strippedBasename = modelBasename.substr(0, modelBasename.size() - kSdrSuffix.size());
        pool = scanFuzzyTexturePoolForBasename(texturesDir, strippedBasename);
    }
    if (pool.files.empty()) {
        if (auto strippedRaceGender = stripRaceGenderSuffix(modelBasename)) {
            pool = scanFuzzyTexturePoolForBasename(texturesDir, *strippedRaceGender);
        }
    }
    return pool;
}

std::string materialDedupKey(const gltf::Material& gm) {
    std::ostringstream key;
    key << static_cast<int>(gm.alphaMode) << '|' << gm.doubleSided << '|' << gm.unlit << '|';
    for (float c : gm.baseColorFactor) key << c << ',';
    key << '|' << gm.baseColorImageName << '|' << gm.baseColorImagePng.size() << '|'
        << gm.baseColorTexCoord << '|' << gm.textureType << '|' << gm.baseColorTextureFileDataId
        << '|';
    for (const auto& layer : gm.additionalTextureLayers) {
        key << layer.fileDataId << ':' << layer.texCoord << ':' << layer.imagePng.size() << ';';
    }
    key << '|';
    if (gm.textureTransform) {
        const auto& t = *gm.textureTransform;
        key << t.constant << ':' << t.translation.x << ',' << t.translation.y << ','
            << t.translation.z << ':';
        for (float r : t.rotation) key << r << ',';
        key << ':' << t.scaling.x << ',' << t.scaling.y << ',' << t.scaling.z;
    }
    key << '|';
    for (const auto& cand : gm.alternateTextureCandidates) {
        key << cand.filename << ':' << cand.category << ';';
    }
    key << '|';
    auto appendColorCurves = [&](const auto& curves) {
        for (const auto& curve : curves) {
            key << curve.sequenceIndex << '[';
            for (const auto& [t, v] : curve.keyframes) key << t << ':' << v.x << ',' << v.y << ',' << v.z << ';';
            key << ']';
        }
        key << '|';
    };
    auto appendScalarCurves = [&](const auto& curves) {
        for (const auto& curve : curves) {
            key << curve.sequenceIndex << '[';
            for (const auto& [t, v] : curve.keyframes) key << t << ':' << v << ';';
            key << ']';
        }
        key << '|';
    };
    appendColorCurves(gm.tintAnimation);
    appendScalarCurves(gm.alphaFadeAnimation);
    appendScalarCurves(gm.weightFadeAnimation);
    return key.str();
}

}  // namespace husk::commands
