// CLI tier: `husk export`'s texture-resolution cluster -- exercises
// husk::commands::exportBundle by spawning the real compiled binary (see
// run_husk.hpp) against small, synthetic, on-disk fixtures, then reading the
// written bundle's schema-checked manifest. Covers fuzzy/hardcoded-slot
// basename matching, ambiguous-slot defaulting (size/category/blend-mode
// ranking), .blp in-memory decode + --textures-out mirroring, --listfile/
// --listfile-root resolution, and the '_sdr' fallback. See
// TEST_DESIGN.md#Four-tier-architecture for how this tier relates to the
// others.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "run_husk.hpp"
#include "test_bundle_fixtures.hpp"
#include "test_cli_fixtures.hpp"
#include "test_cli_fixtures_scenes.hpp"

using husk::test::runHusk;
using Json = nlohmann::json;

namespace {

// A real, valid 1x1 RGBA PNG, for fixtures whose candidates get their pixel
// dimensions read during ambiguous-slot ranking.
const std::vector<uint8_t> kOnePixelPng = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
    0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
    0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
    0xCF, 0xC0, 0xD0, 0x00, 0x00, 0x04, 0x81, 0x01, 0x80, 0x2C, 0x55, 0xCE, 0xB0, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

std::vector<Json> texturesInState(const Json& manifest, const std::string& state) {
    std::vector<Json> out;
    for (const Json& t : manifest["resources"]["textures"]) {
        if (t["state"] == state) out.push_back(t);
    }
    return out;
}

// The bytes a resolved texture entry's payload points at.
std::string payloadBytes(const fs::path& bundleDir, const Json& texture) {
    REQUIRE(texture.contains("payload"));
    return readTextFile(bundleDir / texture["payload"]["uri"].get<std::string>());
}

// Every resolved texture's payload bytes, concatenated: the bundle-side
// equivalent of "this image reached the export".
std::string allPayloadBytes(const fs::path& bundleDir, const Json& manifest) {
    std::string all;
    for (const Json& t : texturesInState(manifest, "resolved")) {
        if (t.contains("payload")) all += payloadBytes(bundleDir, t);
    }
    return all;
}

// The texture a material's first layer draws.
const Json& primaryLayerTexture(const Json& manifest, const Json& material) {
    return textureAt(manifest, material["layers"][0]["texture"]);
}

std::vector<std::string> candidateNames(const Json& texture) {
    std::vector<std::string> names;
    for (const Json& c : texture["candidates"]) names.push_back(c["identity"]["name"].get<std::string>());
    return names;
}

}  // namespace

TEST_CASE("husk export: --textures defaults to the model's own directory -- a FileDataID-named "
          "file already sitting there is embedded without passing the flag") {
    auto dir = defaultsDir("textures");
    writeFile(dir / "textured.m2", oneTexturedModel(555));
    writeFile(dir / "textured00.skin", oneTexturedModelSkin());
    writeFile(dir / "555.png", {1, 2, 3, 4});  // fake PNG bytes -- husk embeds raw, doesn't decode

    auto result = runHusk("export " + (dir / "textured.m2").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    auto bundle = dir / "textured.bundle";
    Json manifest = parseManifest(bundle);
    auto resolved = texturesInState(manifest, "resolved");
    REQUIRE(resolved.size() == 1);
    CHECK(resolved[0]["identity"]["id"]["value"] == 555);
    CHECK(payloadBytes(bundle, resolved[0]) == std::string("\x01\x02\x03\x04", 4));

    fs::remove_all(dir);
}

TEST_CASE("husk export: --textures none never embeds an image, even when a matching "
          "<FileDataID>.png sits in the default (model's own) directory") {
    auto dir = defaultsDir("texturesnone");
    writeFile(dir / "textured.m2", oneTexturedModel(555));
    writeFile(dir / "textured00.skin", oneTexturedModelSkin());
    writeFile(dir / "555.png", {1, 2, 3, 4});

    auto result = runHusk("export " + (dir / "textured.m2").string() + " --textures none");
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    auto bundle = dir / "textured.bundle";
    Json manifest = parseManifest(bundle);
    CHECK(texturesInState(manifest, "resolved").empty());
    CHECK(texturesInState(manifest, "known_unresolved").size() == 1);
    CHECK_FALSE(fs::exists(bundle / "textures"));

    fs::remove_all(dir);
}

TEST_CASE("husk export: a hardcoded texture slot's layer carries its real semantic type name") {
    auto dir = defaultsDir("typename");
    writeFile(dir / "typename.m2", oneTexturedModelWithType(6));  // 6 = TEX_COMPONENT_CHAR_HAIR
    writeFile(dir / "typename00.skin", oneTexturedModelSkin());

    auto result = runHusk("export " + (dir / "typename.m2").string() + " -o " + (dir / "out").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    Json manifest = parseManifest(dir / "out");
    REQUIRE(manifest["resources"]["materials"].size() == 1);
    const Json& layer = manifest["resources"]["materials"][0]["layers"][0];
    CHECK(layer["role"] == "char_hair");
    CHECK(layer["identity"]["name"] == "char_hair");

    fs::remove_all(dir);
}

TEST_CASE("husk export: a hardcoded texture slot embeds the sole basename-matching file in "
          "--textures when husk can't resolve a FileDataID, naming it as a husk-synthesized "
          "guess with no FileDataID to cross-reference") {
    auto dir = defaultsDir("fuzzytex-sole");
    writeFile(dir / "fuzzytex.m2", oneTexturedModelWithType(1));  // 1 = TEX_COMPONENT_SKIN
    writeFile(dir / "fuzzytex00.skin", oneTexturedModelSkin());
    // No "_hd" suffix -- this model isn't an "_hd" variant, and the fuzzy
    // pool hard-partitions on `_hd`-ness, so a real (non-"_hd") candidate
    // is what this fixture needs.
    writeFile(dir / "fuzzytexfaceupper00_00.png", {1, 2, 3, 4});

    auto result = runHusk("export --explain-textures " + (dir / "fuzzytex.m2").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("fuzzy-same-basename-pool HIT") != std::string::npos);
    CHECK(result.output.find("sole type-compatible candidate") != std::string::npos);

    auto bundle = dir / "fuzzytex.bundle";
    Json manifest = parseManifest(bundle);
    auto resolved = texturesInState(manifest, "resolved");
    REQUIRE(resolved.size() == 1);
    CHECK(resolved[0]["identity"]["name"] == "fuzzytexfaceupper00_00");
    // A same-basename match is a guess, never a verified identity.
    CHECK(resolved[0]["identity"]["name_source"] == "synthesized");
    CHECK(resolved[0]["identity"]["id"]["kind"] == "none");
    CHECK(payloadBytes(bundle, resolved[0]) == std::string("\x01\x02\x03\x04", 4));

    fs::remove_all(dir);
}

TEST_CASE("husk export: a FileDataID-exact texture match is resolved by the literal tier and "
          "prints no warning (deterministic, nothing to double-check)") {
    auto dir = defaultsDir("fdid-no-warning");
    writeFile(dir / "fdidtex.m2", oneTexturedModel(1034713));
    writeFile(dir / "fdidtex00.skin", oneTexturedModelSkin());
    writeFile(dir / "1034713.png", {1, 2, 3, 4});

    auto result = runHusk("export --explain-textures " + (dir / "fdidtex.m2").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("husk: warning:") == std::string::npos);
    CHECK(result.output.find("fuzzy-same-basename-pool") == std::string::npos);

    Json manifest = parseManifest(dir / "fdidtex.bundle");
    auto resolved = texturesInState(manifest, "resolved");
    REQUIRE(resolved.size() == 1);
    CHECK(resolved[0]["identity"]["id"]["value"] == 1034713);

    fs::remove_all(dir);
}

namespace {

// Minimal valid single-block BLP2 file: DXT1, 4x4, solid red -- enough to
// exercise husk's own in-memory decode path (src/blp.cpp), not a stand-in
// PNG-shaped blob like the other fixtures in this file use (husk never
// inspects .png bytes, but a .blp file's own header/pixel data is real
// input it parses, so this fixture needs to actually be one).
std::vector<uint8_t> oneRedBlpFile() {
    std::vector<uint8_t> f(1172, 0);  // fixed header + palette region
    std::memcpy(f.data(), "BLP2", 4);
    f[0x04] = 1;                                     // version
    f[0x08] = 2;                                      // colorEncoding = DXT
    f[0x09] = 8;                                       // alphaBitDepth
    f[0x0A] = 0;                                       // preferredFormat = DXT1
    f[0x0B] = 1;                                       // hasMipmaps
    f[0x0C] = 4;                                       // width
    f[0x10] = 4;                                       // height
    uint32_t mipOffset = static_cast<uint32_t>(f.size());
    std::memcpy(f.data() + 0x14, &mipOffset, 4);       // mipOffsets[0]
    // DXT1 block: color0 == color1 == pure red (r5=31,g6=0,b5=0), all
    // indices 0 -> every pixel resolves to color0 regardless of mode.
    uint16_t red565 = 31 << 11;
    f.push_back(static_cast<uint8_t>(red565));
    f.push_back(static_cast<uint8_t>(red565 >> 8));
    f.push_back(static_cast<uint8_t>(red565));
    f.push_back(static_cast<uint8_t>(red565 >> 8));
    f.insert(f.end(), {0, 0, 0, 0});
    return f;
}

}  // namespace

TEST_CASE("husk export: a .blp texture is rehoused as DDS with its DXT1 block verbatim, no "
          "--textures-out needed") {
    auto dir = defaultsDir("blp-embed");
    writeFile(dir / "blptex.m2", oneTexturedModel(4242));
    writeFile(dir / "blptex00.skin", oneTexturedModelSkin());
    auto blp = oneRedBlpFile();
    writeFile(dir / "4242.blp", blp);

    auto result = runHusk("export " + (dir / "blptex.m2").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    auto bundle = dir / "blptex.bundle";
    Json manifest = parseManifest(bundle);
    auto resolved = texturesInState(manifest, "resolved");
    REQUIRE(resolved.size() == 1);
    CHECK(resolved[0]["payload"]["media_type"] == "image/vnd-ms.dds");
    std::string dds = payloadBytes(bundle, resolved[0]);
    REQUIRE(dds.size() >= 128 + 8);
    CHECK(dds.substr(0, 4) == "DDS ");
    // Width and height (DDS_HEADER offsets 12/16 after the magic).
    uint32_t height = 0, width = 0;
    std::memcpy(&height, dds.data() + 12, 4);
    std::memcpy(&width, dds.data() + 16, 4);
    CHECK(width == 4);
    CHECK(height == 4);
    // The one DXT1 block, byte for byte: pure red, every index 0.
    std::string block(blp.end() - 8, blp.end());
    CHECK(dds.substr(128, 8) == block);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a corrupted .blp fails to decode, warns, and embeds nothing (not a hard "
          "export failure)") {
    auto dir = defaultsDir("blp-corrupt");
    writeFile(dir / "badblp.m2", oneTexturedModel(4243));
    writeFile(dir / "badblp00.skin", oneTexturedModelSkin());
    writeFile(dir / "4243.blp", {1, 2, 3, 4});  // not a real BLP2 file

    auto result = runHusk("export " + (dir / "badblp.m2").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("husk: warning: failed to decode") != std::string::npos);

    Json manifest = parseManifest(dir / "badblp.bundle");
    CHECK(texturesInState(manifest, "resolved").empty());
    CHECK(texturesInState(manifest, "known_unresolved").size() == 1);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --textures-out mirrors a decoded .blp as a real .png, source untouched") {
    auto dir = defaultsDir("blp-textures-out");
    writeFile(dir / "blpout.m2", oneTexturedModel(4244));
    writeFile(dir / "blpout00.skin", oneTexturedModelSkin());
    writeFile(dir / "4244.blp", oneRedBlpFile());
    auto outDir = dir / "converted";

    auto result = runHusk("export " + (dir / "blpout.m2").string() + " --textures-out " +
                           outDir.string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    Json manifest = parseManifest(dir / "blpout.bundle");
    CHECK(texturesInState(manifest, "resolved").size() == 1);

    auto mirroredPng = outDir / "4244.png";
    REQUIRE(fs::exists(mirroredPng));
    CHECK(fs::exists(dir / "4244.blp"));  // source untouched, still the raw .blp

    // The mirrored copy is real PNG bytes, not just a same-named placeholder.
    std::ifstream f(mirroredPng, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    REQUIRE(bytes.size() > 8);
    CHECK(bytes[0] == 0x89);
    CHECK(bytes[1] == 'P');
    CHECK(bytes[2] == 'N');
    CHECK(bytes[3] == 'G');

    fs::remove_all(dir);
}

TEST_CASE("husk export: two basename-matching candidates for one hardcoded slot are kept BOTH as "
          "an ambiguous texture's candidates, not neither") {
    auto dir = defaultsDir("fuzzytex-ambiguous");
    writeFile(dir / "fuzzytex.m2", oneTexturedModelWithType(1));
    writeFile(dir / "fuzzytex00.skin", oneTexturedModelSkin());
    // Two real type-1 (skin) tag-conjunction hits ("face" and "skin" both
    // satisfy textureTypeTagClauses' type-1 OR-of-clauses) -- neither
    // "_hd" (this model isn't an "_hd" variant, and the fuzzy pool hard-
    // partitions on that).
    writeFile(dir / "fuzzytexfaceupper00_00.png", kOnePixelPng);
    writeFile(dir / "fuzzytexskin00_00.png", kOnePixelPng);

    auto result = runHusk("export " + (dir / "fuzzytex.m2").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    Json manifest = parseManifest(dir / "fuzzytex.bundle");
    REQUIRE(manifest["resources"]["materials"].size() == 1);
    const Json& texture = primaryLayerTexture(manifest, manifest["resources"]["materials"][0]);
    CHECK(texture["state"] == "ambiguous");
    auto names = candidateNames(texture);
    REQUIRE(names.size() == 2);
    CHECK(std::find(names.begin(), names.end(), "fuzzytexfaceupper00_00.png") != names.end());
    CHECK(std::find(names.begin(), names.end(), "fuzzytexskin00_00.png") != names.end());
    for (const Json& c : texture["candidates"]) {
        CHECK(c["identity"]["name_source"] == "synthesized");
        CHECK(c["width"] == 1);
        CHECK(c["height"] == 1);
    }

    fs::remove_all(dir);
}

TEST_CASE("husk export: two hardcoded slots of genuinely different M2Texture::types each only see "
          "their own type-compatible candidates from the shared fuzzy pool, not each other's "
          "(CLAUDE_HISTORY.md: a jewelry-color file must never end up offered to a skin "
          "slot just because both slots are independently ambiguous)") {
    auto dir = defaultsDir("category-filtered-fuzzy");
    writeFile(dir / "twohard.m2", twoHardcodedTexturedModel(1, 20));  // 1=skin, 20=char_jewelry
    writeFile(dir / "twohard00.skin", twoBatchSkin());
    writeFile(dir / "twohard_skin_color_1000.png", kOnePixelPng);
    writeFile(dir / "twohard_skin_color_1001.png", kOnePixelPng);
    writeFile(dir / "twohard_jewelry_color_2000.png", kOnePixelPng);
    writeFile(dir / "twohard_jewelry_color_2001.png", kOnePixelPng);

    auto result = runHusk("export " + (dir / "twohard.m2").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    Json manifest = parseManifest(dir / "twohard.bundle");
    const Json& materials = manifest["resources"]["materials"];
    REQUIRE(materials.size() == 2);

    bool sawJewelry = false;
    bool sawSkin = false;
    for (const Json& mat : materials) {
        bool isJewelry = mat["layers"][0]["role"] == "char_jewelry";
        sawJewelry |= isJewelry;
        sawSkin |= !isJewelry;
        const Json& texture = primaryLayerTexture(manifest, mat);
        REQUIRE(texture["state"] == "ambiguous");
        auto names = candidateNames(texture);
        REQUIRE(names.size() == 2);  // each slot's own pair, never the other slot's
        for (const std::string& name : names) {
            if (isJewelry) {
                CHECK(name.find("jewelry_color") != std::string::npos);
                CHECK(name.find("skin_color") == std::string::npos);
            } else {
                CHECK(name.find("skin_color") != std::string::npos);
                CHECK(name.find("jewelry_color") == std::string::npos);
            }
        }
    }
    CHECK(sawJewelry);
    CHECK(sawSkin);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a char_jewelry (type 20) slot only offers 'jewelry_color'-category "
          "candidates, not 'body_jewelry'/'bracelets' ones (LUNA_FINDINGS.md: real Blender "
          "verification against bloodelffemale_hd found only jewelry_color_3613861/_3613862 are "
          "actually correct for its one char_jewelry material -- body_jewelry_3602029 turned out "
          "to be a visually distinct necklace-chain item, not a color variant of the same design, "
          "so husk has no confirmed type for it and shouldn't claim type 20 compatibility)") {
    auto dir = defaultsDir("jewelry-color-only");
    writeFile(dir / "jewtest.m2", oneTexturedModelWithType(20));  // 20 = TEX_COMPONENT_CHAR_JEWELRY
    writeFile(dir / "jewtest00.skin", oneTexturedModelSkin());
    writeFile(dir / "jewtest_jewelry_color_1000.png", kOnePixelPng);
    writeFile(dir / "jewtest_jewelry_color_1001.png", kOnePixelPng);
    writeFile(dir / "jewtest_body_jewelry_2000.png", kOnePixelPng);
    writeFile(dir / "jewtest_bracelets_3000.png", kOnePixelPng);

    auto result = runHusk("export " + (dir / "jewtest.m2").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    Json manifest = parseManifest(dir / "jewtest.bundle");
    REQUIRE(manifest["resources"]["materials"].size() == 1);
    const Json& texture = primaryLayerTexture(manifest, manifest["resources"]["materials"][0]);
    REQUIRE(texture["state"] == "ambiguous");
    // Only the two jewelry_color candidates -- body_jewelry/bracelets
    // excluded entirely, not merely deprioritized.
    auto names = candidateNames(texture);
    REQUIRE(names.size() == 2);
    for (const std::string& name : names) CHECK(name.find("jewelry_color") != std::string::npos);
    std::string text = manifest.dump();
    CHECK(text.find("body_jewelry") == std::string::npos);
    CHECK(text.find("bracelets") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: an ambiguous slot's default prefers the largest real candidate, and among "
          "same-size candidates prefers 'skin_color' as the true base layer over an overlay -- "
          "real bug: the same 'skin_color' category token can span both several 1024x512 full-body "
          "atlases AND several unrelated 256x128 underwear-strap decals on bloodelffemale_hd, so "
          "category alone wasn't enough, and a same-resolution overlay like body_jewelry can tie "
          "with the real atlas on pixel area alone") {
    auto dir = defaultsDir("size-ranked-default");
    writeFile(dir / "sizetest.m2", oneTexturedModelWithType(1));  // 1 = TEX_COMPONENT_SKIN
    writeFile(dir / "sizetest00.skin", oneTexturedModelSkin());
    // Small skin_color decal -- must lose to the larger one below.
    writeFile(dir / "sizetest_skin_color_1000.png", solidColorPng(2, 2, 200, 150, 100));
    // Large skin_color atlas -- the real base layer, must win overall.
    writeFile(dir / "sizetest_skin_color_1001.png", solidColorPng(8, 8, 200, 150, 100));
    // Same-size overlay -- ties on pixel area with the atlas above, must
    // still lose to it (skin_color is the base layer, this is layered on
    // top of it, never the right default by itself). Carries the "skin"
    // tag too (textureTypeTagClauses' type-1 query) so it still enters the
    // candidate set at all under the tag conjunction -- a real overlay
    // filename co-occurring with "skin" the way real corpus names like
    // "nakedtorsoskin..." do, not a synthetic-only shape.
    writeFile(dir / "sizetest_skin_body_jewelry_2000.png", solidColorPng(8, 8, 220, 190, 60));

    auto result = runHusk("export " + (dir / "sizetest.m2").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    Json manifest = parseManifest(dir / "sizetest.bundle");
    REQUIRE(manifest["resources"]["materials"].size() == 1);
    const Json& texture = primaryLayerTexture(manifest, manifest["resources"]["materials"][0]);
    REQUIRE(texture["state"] == "ambiguous");
    // Candidates are listed in default-preference order: the first is the
    // one husk would pick.
    REQUIRE(texture["candidates"].size() == 3);
    CHECK(texture["candidates"][0]["identity"]["name"] == "sizetest_skin_color_1001.png");
    CHECK(texture["candidates"][0]["width"] == 8);
    CHECK(texture["candidates"][0]["height"] == 8);

    fs::remove_all(dir);
}

TEST_CASE("husk export: an ambiguous slot's candidates drop a bare/unlabeled one sharing the "
          "model's basename once a real recognized-category candidate exists (real bug: "
          "bloodelffemale_hd's own bare '<model>_<fdid>.blp' file turned out to be an unrelated "
          "small icon, not a skin texture, while the real skin atlas was sitting under the "
          "recognized 'skin_color' category the whole time)") {
    auto dir = defaultsDir("bare-vs-category");
    writeFile(dir / "baretest.m2", oneTexturedModelWithType(1));  // 1 = TEX_COMPONENT_SKIN
    writeFile(dir / "baretest00.skin", oneTexturedModelSkin());
    // Bare candidate -- alphabetically first (numeric sorts before letters),
    // so alphabetical order alone would pick it.
    writeFile(dir / "baretest_1000.png", kOnePixelPng);
    writeFile(dir / "baretest_skin_color_2000.png", kOnePixelPng);

    auto result = runHusk("export " + (dir / "baretest.m2").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    Json manifest = parseManifest(dir / "baretest.bundle");
    REQUIRE(manifest["resources"]["materials"].size() == 1);
    // filterCandidatesForType drops the bare file entirely once a
    // recognized "skin_color" candidate exists, so the slot narrows to a
    // sole match and resolves deterministically -- not ambiguous at all.
    const Json& texture = primaryLayerTexture(manifest, manifest["resources"]["materials"][0]);
    CHECK(texture["state"] == "resolved");
    CHECK(texture["identity"]["name"] == "baretest_skin_color_2000");
    CHECK(manifest.dump().find("baretest_1000") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: an additive-blend (blendMode > 2) batch's ambiguous slot prefers a "
          "'_glow'-suffixed candidate as its default, while a normal opaque batch sharing the exact "
          "same same-basename pool still avoids it -- real bug: creature/tripod2.m2 has a base-skin "
          "batch (blendMode 0) and a separate additive glow batch (blendMode 4), both referencing a "
          "replaceable texture type with no filename/FileDataID, both drawing from the same "
          "{blue,blue_glow}-style pool -- before this fix both batches picked the same non-glow "
          "candidate, so the glow batch rendered with a flat base-color texture instead of its own "
          "real masked emissive image") {
    auto dir = defaultsDir("glow-blend-mode");
    // type 11/12 mirror the real tripod2.m2 fixture (monster_1/monster_2,
    // wowdev.wiki replaceable-texture types) -- the exact type values don't
    // matter for this test beyond being distinct and unrecognized by
    // candidateCategoryTypes (so neither slot's pool gets category-filtered
    // away, same as a real creature model with no character-customization
    // category vocabulary in its texture directory).
    writeFile(dir / "glowtest.m2", twoHardcodedTexturedModelWithBlendModes(11, 0, 12, 4));
    writeFile(dir / "glowtest00.skin", twoBatchSkinDifferentMaterials());
    writeFile(dir / "glowtest_blue.png", solidColorPng(4, 4, 100, 100, 200));
    writeFile(dir / "glowtest_blue_glow.png", solidColorPng(4, 4, 10, 10, 10));

    auto result = runHusk("export " + (dir / "glowtest.m2").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    Json manifest = parseManifest(dir / "glowtest.bundle");
    const Json& materials = manifest["resources"]["materials"];
    REQUIRE(materials.size() == 2);

    bool sawAdditive = false;
    bool sawOpaque = false;
    for (const Json& mat : materials) {
        // M2 blendMode 4 is the additive framebuffer blend, 0 opaque.
        bool isAdditive = mat["framebuffer_blend"] == "add";
        sawAdditive |= isAdditive;
        sawOpaque |= mat["framebuffer_blend"] == "opaque";
        const Json& texture = primaryLayerTexture(manifest, mat);
        REQUIRE(texture["state"] == "ambiguous");
        REQUIRE(texture["candidates"].size() == 2);
        std::string defaultName = texture["candidates"][0]["identity"]["name"].get<std::string>();
        if (isAdditive) {
            CHECK(defaultName == "glowtest_blue_glow.png");
        } else {
            CHECK(defaultName == "glowtest_blue.png");
        }
    }
    CHECK(sawAdditive);
    CHECK(sawOpaque);

    fs::remove_all(dir);
}

TEST_CASE("husk export: two batches resolving to the exact same material (same materialIndex, "
          "same textureComboIndex) share one bundle material instead of getting one each "
          "(CLAUDE_HISTORY.md: real corpus models had ~500 batches producing ~500 materials "
          "where a handful of truly distinct ones would do)") {
    auto dir = defaultsDir("material-dedup");
    writeFile(dir / "dedup.m2", oneTexturedModel(4242));
    writeFile(dir / "dedup00.skin", twoBatchesSameComboSkin());
    writeFile(dir / "4242.png", kOnePixelPng);

    auto result = runHusk("export " + (dir / "dedup.m2").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    Json manifest = parseManifest(dir / "dedup.bundle");
    REQUIRE(manifest["resources"]["materials"].size() == 1);
    const Json& primitives = manifest["resources"]["geometry"]["mesh"]["primitives"];
    REQUIRE(primitives.size() == 2);
    CHECK(primitives[0]["material_index"] == 0);
    CHECK(primitives[1]["material_index"] == 0);

    fs::remove_all(dir);
}

TEST_CASE("husk export: an ordinary FileDataID-resolvable texture (textureType 0, the common case "
          "for a non-character prop/weapon model, no --db2-dir/--customization data at all) keeps "
          "its FileDataID as its identity and a diffuse role -- not an empty or generic one") {
    auto dir = defaultsDir("plain-fallback-name");
    writeFile(dir / "plaintex.m2", oneTexturedModel(4343));
    writeFile(dir / "plaintex00.skin", oneTexturedModelSkin());
    writeFile(dir / "4343.png", kOnePixelPng);

    auto result = runHusk("export " + (dir / "plaintex.m2").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    Json manifest = parseManifest(dir / "plaintex.bundle");
    REQUIRE(manifest["resources"]["materials"].size() == 1);
    const Json& mat = manifest["resources"]["materials"][0];
    CHECK(mat["layers"][0]["role"] == "diffuse");
    const Json& texture = primaryLayerTexture(manifest, mat);
    CHECK(texture["state"] == "resolved");
    CHECK(texture["identity"]["id"]["kind"] == "file_data_id");
    CHECK(texture["identity"]["id"]["value"] == 4343);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a fdid-resolvable slot keeps its own FileDataID-named file even when a "
          "hardcoded slot in the same model claims a real-named file from the shared fuzzy pool "
          "(regression: an earlier version of the name-priority fix let this cross-contaminate)") {
    auto dir = defaultsDir("mixedtex");
    writeFile(dir / "mixedtex.m2", twoTexturedModel(1034713));
    writeFile(dir / "mixedtex00.skin", twoBatchSkin());
    // texture[1]'s own FileDataID-named file (deterministic match).
    writeFile(dir / "1034713.png", {'F', 'D', 'I', 'D'});
    // A real-named file sharing the model's basename -- the only thing
    // texture[0]'s hardcoded slot (type 6, char_hair) can ever resolve to,
    // and the only real candidate in the fuzzy pool. Carries the "hair" tag
    // (textureTypeTagClauses' type-6 query) and no "_hd" suffix (this model
    // isn't an "_hd" variant, and the fuzzy pool hard-partitions on that).
    writeFile(dir / "mixedtexhairstyle00_01.png", {'N', 'A', 'M', 'E'});

    auto result = runHusk("export " + (dir / "mixedtex.m2").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    auto bundle = dir / "mixedtex.bundle";
    Json manifest = parseManifest(bundle);
    const Json& materials = manifest["resources"]["materials"];
    REQUIRE(materials.size() == 2);
    bool sawFdid = false;
    bool sawNamed = false;
    for (const Json& mat : materials) {
        const Json& texture = primaryLayerTexture(manifest, mat);
        REQUIRE(texture["state"] == "resolved");
        std::string bytes = payloadBytes(bundle, texture);
        if (mat["layers"][0]["role"] == "char_hair") {
            sawNamed = true;
            CHECK(bytes == "NAME");
            CHECK(texture["identity"]["name"] == "mixedtexhairstyle00_01");
        } else {
            sawFdid = true;
            CHECK(bytes == "FDID");
            // Not renamed to the fuzzy-matched file's name.
            CHECK(texture["identity"]["id"]["value"] == 1034713);
        }
    }
    CHECK(sawFdid);
    CHECK(sawNamed);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a fdid-resolvable slot with no matching \"<fdid>.png\" file falls through "
          "to a real basename-matching file instead of embedding nothing, still recorded under its "
          "real FileDataID") {
    auto dir = defaultsDir("fallbacktex");
    writeFile(dir / "fallbacktex.m2", oneTexturedModel(9999999));  // no 9999999.png on disk
    writeFile(dir / "fallbacktex00.skin", oneTexturedModelSkin());
    // No "_hd" suffix -- this model isn't an "_hd" variant, and the fuzzy
    // pool hard-partitions on that.
    writeFile(dir / "fallbacktexfaceupper00_00.png", {'N', 'A', 'M', 'E'});

    auto result = runHusk("export --explain-textures " + (dir / "fallbacktex.m2").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("fdid=9999999") != std::string::npos);
    CHECK(result.output.find("fuzzy-same-basename-pool HIT") != std::string::npos);

    auto bundle = dir / "fallbacktex.bundle";
    Json manifest = parseManifest(bundle);
    auto resolved = texturesInState(manifest, "resolved");
    REQUIRE(resolved.size() == 1);
    CHECK(payloadBytes(bundle, resolved[0]) == "NAME");
    // The real FileDataID is still the identity, even though the bytes came
    // from the named file, not "9999999.png".
    CHECK(resolved[0]["identity"]["id"]["value"] == 9999999);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a fdid-resolvable slot with no matching \"<fdid>.png\" and no --listfile "
          "embeds nothing (baseline for the --listfile test below, proves the fixture alone isn't "
          "enough -- the real file lives away from both the model dir and any bare-fdid name)") {
    auto dir = defaultsDir("nolistfiletex");
    writeFile(dir / "nolistfiletex.m2", oneTexturedModel(1018799));
    writeFile(dir / "nolistfiletex00.skin", oneTexturedModelSkin());
    auto corpusRoot = dir / "corpus";
    fs::create_directories(corpusRoot / "character/human/male");
    writeFile(corpusRoot / "character/human/male/deathknighteyeglow.png", {'L', 'I', 'S', 'T'});

    auto result = runHusk("export " + (dir / "nolistfiletex.m2").string() +
                           " --textures " + corpusRoot.string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    auto bundle = dir / "nolistfiletex.bundle";
    Json manifest = parseManifest(bundle);
    CHECK(texturesInState(manifest, "resolved").empty());
    CHECK(allPayloadBytes(bundle, manifest).find("LIST") == std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --listfile resolves a FileDataID to its real corpus-relative path when no "
          "exact \"<fdid>.{blp,png}\" file exists next to --textures (casc-tool FAILURES.md item "
          "13's real motivating case: most textures in a real export sit under their own real "
          "name/path, not renamed to a bare FileDataID)") {
    auto dir = defaultsDir("listfiletex");
    writeFile(dir / "listfiletex.m2", oneTexturedModel(1018799));
    writeFile(dir / "listfiletex00.skin", oneTexturedModelSkin());
    auto corpusRoot = dir / "corpus";
    fs::create_directories(corpusRoot / "character/human/male");
    // Deliberately NOT named "1018799.png" -- this is the real listfile
    // name/path the exact-FileDataID match can never find on its own.
    writeFile(corpusRoot / "character/human/male/deathknighteyeglow.png", {'L', 'I', 'S', 'T'});
    // The listfile itself names the ".blp" extension (matching a real
    // community-listfile.csv, which always names the CASC-side source
    // format) even though the file on disk is a ".png" -- proves the
    // extension gets swapped via the same resolveTextureBytes() path the
    // exact-fdid match already uses, not a literal string match.
    auto listfilePath = dir / "listfile.csv";
    {
        std::ofstream f(listfilePath);
        f << "1018799;character/human/male/deathknighteyeglow.blp\n";
    }

    auto result = runHusk("export " + (dir / "listfiletex.m2").string() +
                           " --textures " + corpusRoot.string() +
                           " --listfile " + listfilePath.string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    auto bundle = dir / "listfiletex.bundle";
    Json manifest = parseManifest(bundle);
    auto resolved = texturesInState(manifest, "resolved");
    REQUIRE(resolved.size() == 1);
    CHECK(payloadBytes(bundle, resolved[0]) == "LIST");
    // Still recorded under its real FileDataID, same as the exact-match
    // case, and named from the listfile.
    CHECK(resolved[0]["identity"]["id"]["value"] == 1018799);
    CHECK(resolved[0]["identity"]["name_source"] == "listfile");

    fs::remove_all(dir);
}

TEST_CASE("husk export: a --listfile that opens fine but has no usable entries (every line "
          "malformed, or genuinely empty) warns loudly instead of silently degrading -- the export "
          "itself still succeeds via local-only fallback, same as if --listfile had never been "
          "given") {
    auto dir = defaultsDir("corruptlistfile");
    writeFile(dir / "corruptlistfile.m2", tinyValidM2());
    writeFile(dir / "corruptlistfile00.skin", tinyMatchingSkin());
    auto listfilePath = dir / "listfile.csv";
    {
        std::ofstream f(listfilePath);
        // No ';' separator anywhere, no numeric ID -- every line here fails
        // loadListfile's own parse, the "wrong file entirely" / "corrupted
        // mid-download" case this warning exists for.
        f << "this is not a listfile\nneither is this\n";
    }

    auto result = runHusk("export " + (dir / "corruptlistfile.m2").string() +
                           " --listfile " + listfilePath.string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("warning: --listfile") != std::string::npos);
    CHECK(result.output.find("no usable entries") != std::string::npos);
    CHECK(fs::exists(dir / "corruptlistfile.bundle" / "manifest.json"));

    fs::remove_all(dir);
}

TEST_CASE("husk export: --listfile-root is independent of --textures -- co-located same-basename "
          "matching keeps using --textures (the model's own directory) while the listfile fallback "
          "reaches into a completely separate corpus root (the real render-job shape: a model's "
          "directory has its own co-located textures, while a FileDataID-only slot's real file "
          "lives elsewhere in the tree entirely)") {
    auto dir = defaultsDir("splitroottex");
    writeFile(dir / "splitroottex.m2", twoTexturedModel(1018799));
    writeFile(dir / "splitroottex00.skin", twoBatchSkin());
    // The hardcoded slot's only real candidate: co-located with the model,
    // found via --textures defaulting to the model's own directory. type 6
    // (char_hair) needs the "hair" tag; no "_hd" suffix since this model
    // isn't an "_hd" variant, and the fuzzy pool hard-partitions on that.
    writeFile(dir / "splitroottexhairstyle00_01.png", {'N', 'A', 'M', 'E'});
    // The fdid-resolvable slot's real file lives under a *separate* corpus
    // root, nowhere near the model's own directory -- reachable only via
    // --listfile-root, never via --textures.
    auto corpusRoot = dir / "far_away_corpus_root";
    fs::create_directories(corpusRoot / "character/human/male");
    writeFile(corpusRoot / "character/human/male/deathknighteyeglow.png", {'F', 'D', 'I', 'D'});
    auto listfilePath = dir / "listfile.csv";
    {
        std::ofstream f(listfilePath);
        f << "1018799;character/human/male/deathknighteyeglow.blp\n";
    }

    auto result = runHusk("export " + (dir / "splitroottex.m2").string() +
                           " --listfile " + listfilePath.string() +
                           " --listfile-root " + corpusRoot.string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    auto bundle = dir / "splitroottex.bundle";
    Json manifest = parseManifest(bundle);
    // Both real images present -- neither root's own file was left behind.
    CHECK(texturesInState(manifest, "resolved").size() == 2);
    std::string all = allPayloadBytes(bundle, manifest);
    CHECK(all.find("NAME") != std::string::npos);
    CHECK(all.find("FDID") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --listfile pointed at a nonexistent path fails loudly, not silently") {
    auto dir = defaultsDir("listfilebadpath");
    writeFile(dir / "listfilebadpath.m2", oneTexturedModel(1018799));
    writeFile(dir / "listfilebadpath00.skin", oneTexturedModelSkin());

    auto result = runHusk("export " + (dir / "listfilebadpath.m2").string() +
                           " --listfile " + (dir / "does_not_exist.csv").string());
    CHECK(result.exitCode != 0);
    CHECK(result.output.find("--listfile") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a '_sdr' model's hardcoded texture slot falls back to the non-'_sdr' "
          "basename when the exact '_sdr'-prefixed basename has no same-basename candidates "
          "(real, confirmed naming convention -- '_sdr' stand-in models share textures with "
          "their non-'_sdr' counterpart, never their own '_sdr'-prefixed name)") {
    auto dir = defaultsDir("sdrtex");
    writeFile(dir / "creaturemale_sdr.m2", oneTexturedModelWithType(1));  // type=1 (skin), hardcoded
    writeFile(dir / "creaturemale_sdr00.skin", oneTexturedModelSkin());
    // No "creaturemale_sdr_*" file exists anywhere -- only the non-"_sdr"
    // basename has a real candidate, exactly the real corpus shape.
    writeFile(dir / "creaturemaleskin00_00.png", {'S', 'D', 'R', 'F'});

    auto result = runHusk("export " + (dir / "creaturemale_sdr.m2").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    auto bundle = dir / "creaturemale_sdr.bundle";
    Json manifest = parseManifest(bundle);
    auto resolved = texturesInState(manifest, "resolved");
    REQUIRE(resolved.size() == 1);
    CHECK(payloadBytes(bundle, resolved[0]) == "SDRF");

    fs::remove_all(dir);
}

TEST_CASE("husk export: a '_sdr' model with no non-'_sdr' fallback candidates either embeds "
          "nothing, same as any other genuinely unresolvable hardcoded slot (the fallback is "
          "additive, never a substitute for real data existing somewhere)") {
    auto dir = defaultsDir("sdrtexnothing");
    writeFile(dir / "lonelymale_sdr.m2", oneTexturedModelWithType(1));
    writeFile(dir / "lonelymale_sdr00.skin", oneTexturedModelSkin());
    // No candidate under either basename.

    auto result = runHusk("export " + (dir / "lonelymale_sdr.m2").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    Json manifest = parseManifest(dir / "lonelymale_sdr.bundle");
    CHECK(texturesInState(manifest, "resolved").empty());
    CHECK(texturesInState(manifest, "known_unresolved").size() == 1);

    fs::remove_all(dir);
}

TEST_CASE("husk export --explain-textures: the resolution ledger is a read operation (I4), "
          "naming the tier and file behind every slot -- and silent without the flag") {
    auto dir = defaultsDir("explaintextures");
    writeFile(dir / "explainmale.m2", oneTexturedModelWithType(1));
    writeFile(dir / "explainmale00.skin", oneTexturedModelSkin());
    writeFile(dir / "explainmale_skin_body.png", kOnePixelPng);

    auto quiet = runHusk("export " + (dir / "explainmale.m2").string());
    CHECK(quiet.exitCode == 0);
    CHECK(quiet.output.find("sources::Catalog:") == std::string::npos);

    auto loud = runHusk("export --explain-textures " + (dir / "explainmale.m2").string());
    CHECK(loud.exitCode == 0);
    CHECK(loud.output.find("sources::Catalog:") != std::string::npos);
    // The tier that actually answered, and the file it picked -- not just
    // "a texture was found".
    CHECK(loud.output.find("fuzzy-same-basename-pool HIT") != std::string::npos);
    CHECK(loud.output.find("explainmale_skin_body") != std::string::npos);

    fs::remove_all(dir);
}
