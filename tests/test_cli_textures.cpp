// CLI tier: `husk export`'s texture-resolution cluster -- exercises
// husk::commands::exportGlb by spawning the real compiled binary (see
// run_husk.hpp) against small, synthetic, on-disk fixtures. Split out of
// tests/test_cli.cpp (FILE_SPLIT_TODO.md Item 3) -- covers fuzzy/hardcoded-
// slot basename matching, ambiguous-slot defaulting (size/category/blend-mode
// ranking), .blp in-memory decode + --textures-out mirroring, --listfile/
// --listfile-root resolution, and the '_sdr' fallback. See
// TEST_DESIGN.md#Four-tier-architecture for how this tier relates to the
// others.

#include <cstdint>
#include <cstring>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <tiny_gltf.h>
#include <vector>

#include "run_husk.hpp"
#include "test_cli_fixtures.hpp"
#include "test_cli_fixtures_scenes.hpp"

using husk::test::runHusk;
namespace fs = std::filesystem;

TEST_CASE("husk export: --textures defaults to the model's own directory -- a FileDataID-named "
          "file already sitting there is embedded without passing the flag") {
    auto dir = defaultsDir("textures");
    writeFile(dir / "textured.m2", oneTexturedModel(555));
    writeFile(dir / "textured00.skin", oneTexturedModelSkin());
    writeFile(dir / "555.png", {1, 2, 3, 4});  // fake PNG bytes -- husk embeds raw, doesn't decode

    auto result = runHusk("export " + (dir / "textured.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 with an embedded texture") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: --textures none never embeds an image, even when a matching "
          "<FileDataID>.png sits in the default (model's own) directory") {
    auto dir = defaultsDir("texturesnone");
    writeFile(dir / "textured.m2", oneTexturedModel(555));
    writeFile(dir / "textured00.skin", oneTexturedModelSkin());
    writeFile(dir / "555.png", {1, 2, 3, 4});

    auto result = runHusk("export " + (dir / "textured.m2").string() + " --textures none");
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("0 with an embedded texture") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a hardcoded texture slot's material name includes its real semantic "
          "type name") {
    auto m2Path = tempPath("typename.m2");
    writeFile(m2Path, oneTexturedModelWithType(6));  // 6 = TEX_COMPONENT_CHAR_HAIR
    auto skinPath = tempPath("typename.skin");
    writeFile(skinPath, oneTexturedModelSkin());

    auto result = runHusk("export " + m2Path.string() + " --skin " + skinPath.string() + " -o " +
                           tempPath("typename.glb").string());
    CHECK(result.exitCode == 0);

    fs::path glbPath = tempPath("typename.glb");
    std::ifstream glbFile(glbPath, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(glbFile)), std::istreambuf_iterator<char>());
    CHECK(text.find("char_hair") != std::string::npos);

    fs::remove(m2Path);
    fs::remove(skinPath);
    fs::remove(glbPath);
}

TEST_CASE("husk export: a hardcoded texture slot embeds the sole basename-matching file in "
          "--textures when husk can't resolve a FileDataID, and warns that the match is "
          "non-deterministic and has no FileDataID to cross-reference") {
    auto dir = defaultsDir("fuzzytex-sole");
    writeFile(dir / "fuzzytex.m2", oneTexturedModelWithType(1));  // 1 = TEX_COMPONENT_SKIN
    writeFile(dir / "fuzzytex00.skin", oneTexturedModelSkin());
    writeFile(dir / "fuzzytexfaceupper00_00_hd.png", {1, 2, 3, 4});

    auto result = runHusk("export " + (dir / "fuzzytex.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 with an embedded texture") != std::string::npos);
    CHECK(result.output.find("husk: warning:") != std::string::npos);
    CHECK(result.output.find("fuzzytexfaceupper00_00_hd.png") != std::string::npos);
    CHECK(result.output.find("non-deterministic basename matching") != std::string::npos);
    CHECK(result.output.find("no FileDataID at all for this hardcoded slot") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a FileDataID-exact texture match prints no fuzzy-match warning "
          "(deterministic, nothing to double-check)") {
    auto dir = defaultsDir("fdid-no-warning");
    writeFile(dir / "fdidtex.m2", oneTexturedModel(1034713));
    writeFile(dir / "fdidtex00.skin", oneTexturedModelSkin());
    writeFile(dir / "1034713.png", {1, 2, 3, 4});

    auto result = runHusk("export " + (dir / "fdidtex.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 with an embedded texture") != std::string::npos);
    CHECK(result.output.find("husk: warning:") == std::string::npos);

    fs::remove_all(dir);
}

namespace {

// Minimal valid single-block BLP2 file: DXT1, 4x4, solid red -- enough to
// exercise husk's own in-memory decode path (src/blp.cpp), not a stand-in
// PNG-shaped blob like the other fixtures in this file use (husk never
// inspects .png bytes, but a .blp file's own header/pixel data is real
// input it now parses, so this fixture needs to actually be one).
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

TEST_CASE("husk export: a .blp texture is decoded and embedded in-memory, no --textures-out "
          "needed") {
    auto dir = defaultsDir("blp-embed");
    writeFile(dir / "blptex.m2", oneTexturedModel(4242));
    writeFile(dir / "blptex00.skin", oneTexturedModelSkin());
    writeFile(dir / "4242.blp", oneRedBlpFile());

    auto result = runHusk("export " + (dir / "blptex.m2").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 with an embedded texture") != std::string::npos);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string gltfErr, gltfWarn;
    auto glbPath = dir / "blptex.glb";
    bool loaded = loader.LoadBinaryFromFile(&model, &gltfErr, &gltfWarn, glbPath.string());
    INFO("tinygltf error: ", gltfErr);
    REQUIRE(loaded);
    REQUIRE(model.images.size() == 1);
    CHECK(model.images[0].width == 4);
    CHECK(model.images[0].height == 4);
    CHECK(model.images[0].component == 4);
    // Decoded straight from the DXT1 block above: pure red, fully opaque.
    CHECK(model.images[0].image[0] == 255);
    CHECK(model.images[0].image[1] == 0);
    CHECK(model.images[0].image[2] == 0);
    CHECK(model.images[0].image[3] == 255);

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
    CHECK(result.output.find("0 with an embedded texture") != std::string::npos);
    CHECK(result.output.find("husk: warning: failed to decode") != std::string::npos);

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
    CHECK(result.output.find("1 with an embedded texture") != std::string::npos);

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

TEST_CASE("husk export: two basename-matching candidates for one hardcoded slot embed BOTH as "
          "alternate_textures, with one arbitrary default, not neither") {
    // A real, valid 1x1 PNG (not just arbitrary bytes) -- this test actually
    // loads the resulting .glb via tinygltf below, which decodes embedded
    // images and fails the whole load on genuinely invalid image data.
    std::vector<uint8_t> onePixelPng = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
        0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
        0xCF, 0xC0, 0xD0, 0x00, 0x00, 0x04, 0x81, 0x01, 0x80, 0x2C, 0x55, 0xCE, 0xB0, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

    auto dir = defaultsDir("fuzzytex-ambiguous");
    writeFile(dir / "fuzzytex.m2", oneTexturedModelWithType(1));
    writeFile(dir / "fuzzytex00.skin", oneTexturedModelSkin());
    writeFile(dir / "fuzzytexfaceupper00_00_hd.png", onePixelPng);
    writeFile(dir / "fuzzytexhair00_00.png", onePixelPng);

    auto result = runHusk("export " + (dir / "fuzzytex.m2").string());
    CHECK(result.exitCode == 0);
    // Exactly one embedded baseColorTexture (the arbitrary default) --
    // the two candidates aren't guessed *between*, but they're also no
    // longer silently dropped.
    CHECK(result.output.find("1 with an embedded texture") != std::string::npos);
    CHECK(result.output.find("2 same-basename texture candidate(s)") != std::string::npos);
    CHECK(result.output.find("alternate_textures") != std::string::npos);
    CHECK(result.output.find("fuzzytexfaceupper00_00_hd.png") != std::string::npos);
    CHECK(result.output.find("fuzzytexhair00_00.png") != std::string::npos);

    // Real content check: both candidates' actual bytes reached the .glb,
    // not just their names in the warning text.
    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;
    REQUIRE(loader.LoadBinaryFromFile(&model, &err, &warn, (dir / "fuzzytex.glb").string()));
    REQUIRE(model.materials.size() == 1);
    REQUIRE(model.materials[0].extras.Has("alternate_textures"));
    auto alt = model.materials[0].extras.Get("alternate_textures");
    CHECK(alt.ArrayLen() == 2);

    fs::remove_all(dir);
}

TEST_CASE("husk export: two hardcoded slots of genuinely different M2Texture::types each only see "
          "their own type-compatible candidates from the shared fuzzy pool, not each other's "
          "(EYES_ON_FINDINGS.md #3/#6: a jewelry-color file must never end up offered to a skin "
          "slot's alternate_textures just because both slots are independently ambiguous)") {
    std::vector<uint8_t> onePixelPng = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
        0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
        0xCF, 0xC0, 0xD0, 0x00, 0x00, 0x04, 0x81, 0x01, 0x80, 0x2C, 0x55, 0xCE, 0xB0, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

    auto dir = defaultsDir("category-filtered-fuzzy");
    writeFile(dir / "twohard.m2", twoHardcodedTexturedModel(1, 20));  // 1=skin, 20=char_jewelry
    writeFile(dir / "twohard00.skin", twoBatchSkin());
    writeFile(dir / "twohard_skin_color_1000.png", onePixelPng);
    writeFile(dir / "twohard_skin_color_1001.png", onePixelPng);
    writeFile(dir / "twohard_jewelry_color_2000.png", onePixelPng);
    writeFile(dir / "twohard_jewelry_color_2001.png", onePixelPng);

    auto result = runHusk("export " + (dir / "twohard.m2").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;
    REQUIRE(loader.LoadBinaryFromFile(&model, &err, &warn, (dir / "twohard.glb").string()));
    REQUIRE(model.materials.size() == 2);

    for (const auto& mat : model.materials) {
        REQUIRE(mat.extras.Has("alternate_textures"));
        auto alt = mat.extras.Get("alternate_textures");
        REQUIRE(alt.ArrayLen() == 2);  // each slot's own pair, never the other slot's

        // Exactly two materials in this fixture, one per textureType --
        // "char_jewelry" (m2::textureTypeName(20)) unambiguously identifies
        // the jewelry slot's material name, the other is the skin slot's.
        bool isJewelry = mat.name.find("char_jewelry") != std::string::npos;

        for (size_t i = 0; i < alt.ArrayLen(); ++i) {
            std::string filename = alt.Get(i).Get("filename").Get<std::string>();
            if (isJewelry) {
                CHECK(filename.find("jewelry_color") != std::string::npos);
                CHECK(filename.find("skin_color") == std::string::npos);
            } else {
                CHECK(filename.find("skin_color") != std::string::npos);
                CHECK(filename.find("jewelry_color") == std::string::npos);
            }
            // Each candidate's embedded glTF image is named after its own
            // real source filename (stem), not left blank -- Blender's
            // importer falls back to an auto-generated "Image_<N>" for any
            // unnamed image, which is what prompted this.
            int texIdx = alt.Get(i).Get("texture_index").GetNumberAsInt();
            REQUIRE(texIdx >= 0);
            REQUIRE(static_cast<size_t>(texIdx) < model.textures.size());
            std::string expectedName = fs::path(filename).stem().string();
            CHECK(model.images[model.textures[texIdx].source].name == expectedName);
        }
    }

    fs::remove_all(dir);
}

TEST_CASE("husk export: a char_jewelry (type 20) slot only offers 'jewelry_color'-category "
          "candidates, not 'body_jewelry'/'bracelets' ones (LUNA_FINDINGS.md: real Blender "
          "verification against bloodelffemale_hd found only jewelry_color_3613861/_3613862 are "
          "actually correct for its one char_jewelry material -- body_jewelry_3602029 turned out "
          "to be a visually distinct necklace-chain item, not a color variant of the same design, "
          "so husk has no confirmed type for it and shouldn't claim type 20 compatibility)") {
    std::vector<uint8_t> onePixelPng = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
        0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
        0xCF, 0xC0, 0xD0, 0x00, 0x00, 0x04, 0x81, 0x01, 0x80, 0x2C, 0x55, 0xCE, 0xB0, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

    auto dir = defaultsDir("jewelry-color-only");
    writeFile(dir / "jewtest.m2", oneTexturedModelWithType(20));  // 20 = TEX_COMPONENT_CHAR_JEWELRY
    writeFile(dir / "jewtest00.skin", oneTexturedModelSkin());
    writeFile(dir / "jewtest_jewelry_color_1000.png", onePixelPng);
    writeFile(dir / "jewtest_jewelry_color_1001.png", onePixelPng);
    writeFile(dir / "jewtest_body_jewelry_2000.png", onePixelPng);
    writeFile(dir / "jewtest_bracelets_3000.png", onePixelPng);

    auto result = runHusk("export " + (dir / "jewtest.m2").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;
    REQUIRE(loader.LoadBinaryFromFile(&model, &err, &warn, (dir / "jewtest.glb").string()));
    REQUIRE(model.materials.size() == 1);
    REQUIRE(model.materials[0].extras.Has("alternate_textures"));
    auto alt = model.materials[0].extras.Get("alternate_textures");
    // Only the two jewelry_color candidates -- body_jewelry/bracelets
    // excluded entirely, not merely deprioritized.
    REQUIRE(alt.ArrayLen() == 2);
    for (size_t i = 0; i < alt.ArrayLen(); ++i) {
        std::string filename = alt.Get(i).Get("filename").Get<std::string>();
        CHECK(filename.find("jewelry_color") != std::string::npos);
    }
    CHECK(result.output.find("body_jewelry") == std::string::npos);
    CHECK(result.output.find("bracelets") == std::string::npos);

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
    // top of it, never the right default by itself).
    writeFile(dir / "sizetest_body_jewelry_2000.png", solidColorPng(8, 8, 220, 190, 60));

    auto result = runHusk("export " + (dir / "sizetest.m2").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;
    REQUIRE(loader.LoadBinaryFromFile(&model, &err, &warn, (dir / "sizetest.glb").string()));
    REQUIRE(model.materials.size() == 1);
    CHECK(model.materials[0].name.find("skin_color_1001") != std::string::npos);
    REQUIRE(model.materials[0].extras.Has("alternate_textures"));
    auto alt = model.materials[0].extras.Get("alternate_textures");
    REQUIRE(alt.ArrayLen() > 0);
    CHECK(alt.Get(0).Get("filename").Get<std::string>() == "sizetest_skin_color_1001.png");

    fs::remove_all(dir);
}

TEST_CASE("husk export: an ambiguous slot's default prefers a real recognized-category candidate "
          "over a bare/unlabeled one sharing the model's basename (real bug: bloodelffemale_hd's "
          "own bare '<model>_<fdid>.blp' file turned out to be an unrelated small icon, not a "
          "skin texture, while the real skin atlas was sitting under the recognized 'skin_color' "
          "category the whole time)") {
    std::vector<uint8_t> onePixelPng = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
        0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
        0xCF, 0xC0, 0xD0, 0x00, 0x00, 0x04, 0x81, 0x01, 0x80, 0x2C, 0x55, 0xCE, 0xB0, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

    auto dir = defaultsDir("bare-vs-category");
    writeFile(dir / "baretest.m2", oneTexturedModelWithType(1));  // 1 = TEX_COMPONENT_SKIN
    writeFile(dir / "baretest00.skin", oneTexturedModelSkin());
    // Bare candidate -- alphabetically first (numeric sorts before letters),
    // exactly the shape that used to win by pure alphabetical accident.
    writeFile(dir / "baretest_1000.png", onePixelPng);
    writeFile(dir / "baretest_skin_color_2000.png", onePixelPng);

    auto result = runHusk("export " + (dir / "baretest.m2").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;
    REQUIRE(loader.LoadBinaryFromFile(&model, &err, &warn, (dir / "baretest.glb").string()));
    REQUIRE(model.materials.size() == 1);
    // The bare file is a real unrecognized-category candidate with no
    // evidence behind it -- filterCandidatesForType drops it entirely from
    // this slot's candidate set once a recognized "skin_color" candidate
    // exists (not just deprioritized), so this narrows all the way down to
    // a *sole* match and resolves deterministically -- no alternate_textures
    // ambiguity left to report at all, which is an even stronger outcome
    // than "merely preferred".
    CHECK_FALSE(model.materials[0].extras.Has("alternate_textures"));
    CHECK(model.materials[0].name.find("skin_color") != std::string::npos);
    CHECK(result.output.find("baretest_skin_color_2000.png") != std::string::npos);
    CHECK(result.output.find("baretest_1000.png") == std::string::npos);

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

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;
    REQUIRE(loader.LoadBinaryFromFile(&model, &err, &warn, (dir / "glowtest.glb").string()));
    REQUIRE(model.materials.size() == 2);

    for (const auto& mat : model.materials) {
        // blend_mode extras is only ever present for blendMode > 2
        // (gltf::Material::blendMode's own doc comment) -- its presence
        // alone already tells the two materials in this fixture apart.
        bool isAdditive = mat.extras.Has("blend_mode");
        REQUIRE(mat.extras.Has("alternate_textures"));
        auto alt = mat.extras.Get("alternate_textures");
        REQUIRE(alt.ArrayLen() == 2);
        std::string defaultFilename = alt.Get(0).Get("filename").Get<std::string>();
        if (isAdditive) {
            CHECK(defaultFilename == "glowtest_blue_glow.png");
        } else {
            CHECK(defaultFilename == "glowtest_blue.png");
        }
    }

    fs::remove_all(dir);
}

TEST_CASE("husk export: two batches resolving to the exact same material (same materialIndex, "
          "same textureComboIndex) share one gltf::Material instead of getting one each "
          "(EYES_ON_FINDINGS.md: real corpus models had ~500 batches producing ~500 materials "
          "where a handful of truly distinct ones would do)") {
    std::vector<uint8_t> onePixelPng = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
        0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
        0xCF, 0xC0, 0xD0, 0x00, 0x00, 0x04, 0x81, 0x01, 0x80, 0x2C, 0x55, 0xCE, 0xB0, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

    auto dir = defaultsDir("material-dedup");
    writeFile(dir / "dedup.m2", oneTexturedModel(4242));
    writeFile(dir / "dedup00.skin", twoBatchesSameComboSkin());
    writeFile(dir / "4242.png", onePixelPng);

    auto result = runHusk("export " + (dir / "dedup.m2").string());
    INFO("output:\n", result.output);
    CHECK(result.exitCode == 0);

    tinygltf::TinyGLTF loader;
    tinygltf::Model model;
    std::string err, warn;
    REQUIRE(loader.LoadBinaryFromFile(&model, &err, &warn, (dir / "dedup.glb").string()));
    // One material, shared by both primitives -- not two content-identical
    // ones.
    REQUIRE(model.materials.size() == 1);
    REQUIRE(!model.meshes.empty());
    REQUIRE(model.meshes[0].primitives.size() == 2);
    CHECK(model.meshes[0].primitives[0].material == 0);
    CHECK(model.meshes[0].primitives[1].material == 0);
    // The stored material's name describes what it is, not which batch
    // happened to produce it first -- no "batch0_"/"batch1_" prefix.
    CHECK(model.materials[0].name.find("batch") == std::string::npos);

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
    // texture[0]'s hardcoded slot can ever resolve to, and the only real
    // candidate in the fuzzy pool.
    writeFile(dir / "mixedtexfaceupper00_00_hd.png", {'N', 'A', 'M', 'E'});

    auto result = runHusk("export " + (dir / "mixedtex.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("2 with an embedded texture") != std::string::npos);

    std::ifstream glbFile(dir / "mixedtex.glb", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(glbFile)), std::istreambuf_iterator<char>());
    // Both real images present, each exactly once -- neither slot's file
    // got embedded into the other's material instead.
    CHECK(text.find("FDID") != std::string::npos);
    CHECK(text.find("NAME") != std::string::npos);
    // The fdid-resolvable slot's own material name still carries its real
    // FileDataID -- it wasn't renamed to the fuzzy-matched file's name.
    CHECK(text.find("_fdid1034713") != std::string::npos);
    CHECK(text.find("_mixedtexfaceupper00_00_hd") != std::string::npos);

    fs::remove_all(dir);
}

TEST_CASE("husk export: a fdid-resolvable slot with no matching \"<fdid>.png\" file falls through "
          "to a real basename-matching file instead of embedding nothing (previously: silent gap)") {
    auto dir = defaultsDir("fallbacktex");
    writeFile(dir / "fallbacktex.m2", oneTexturedModel(9999999));  // no 9999999.png on disk
    writeFile(dir / "fallbacktex00.skin", oneTexturedModelSkin());
    writeFile(dir / "fallbacktexfaceupper00_00_hd.png", {'N', 'A', 'M', 'E'});

    auto result = runHusk("export " + (dir / "fallbacktex.m2").string());
    CHECK(result.exitCode == 0);
    CHECK(result.output.find("1 with an embedded texture") != std::string::npos);
    // Fell through to the fuzzy match, so it gets the same warning as any
    // other non-deterministic match -- this time naming the resolved
    // FileDataID it couldn't be cross-checked against, since one exists
    // (unlike a genuinely hardcoded slot).
    CHECK(result.output.find("husk: warning:") != std::string::npos);
    CHECK(result.output.find("resolved FileDataID 9999999") != std::string::npos);

    std::ifstream glbFile(dir / "fallbacktex.glb", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(glbFile)), std::istreambuf_iterator<char>());
    CHECK(text.find("NAME") != std::string::npos);
    // The real FileDataID is still recorded (material name + extras),
    // even though the embedded bytes came from the named file, not
    // "9999999.png" -- see gltf.hpp's Material::baseColorTextureFileDataId
    // doc comment.
    CHECK(text.find("_fdid9999999") != std::string::npos);
    CHECK(text.find("texture_file_data_id") != std::string::npos);

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
    CHECK(result.exitCode == 0);

    std::ifstream glbFile(dir / "nolistfiletex.glb", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(glbFile)), std::istreambuf_iterator<char>());
    CHECK(text.find("LIST") == std::string::npos);

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
    CHECK(result.exitCode == 0);

    std::ifstream glbFile(dir / "listfiletex.glb", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(glbFile)), std::istreambuf_iterator<char>());
    CHECK(text.find("LIST") != std::string::npos);
    // Still recorded under its real FileDataID, same as the exact-match case.
    CHECK(text.find("_fdid1018799") != std::string::npos);

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
    CHECK(fs::exists(dir / "corruptlistfile.glb"));

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
    // found via --textures defaulting to the model's own directory.
    writeFile(dir / "splitroottexfaceupper00_00_hd.png", {'N', 'A', 'M', 'E'});
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
    CHECK(result.exitCode == 0);

    std::ifstream glbFile(dir / "splitroottex.glb", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(glbFile)), std::istreambuf_iterator<char>());
    // Both real images present -- neither root's own file was left behind.
    CHECK(text.find("NAME") != std::string::npos);
    CHECK(text.find("FDID") != std::string::npos);

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
    CHECK(result.exitCode == 0);

    std::ifstream glbFile(dir / "creaturemale_sdr.glb", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(glbFile)), std::istreambuf_iterator<char>());
    CHECK(text.find("SDRF") != std::string::npos);

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
    CHECK(result.exitCode == 0);

    std::ifstream glbFile(dir / "lonelymale_sdr.glb", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(glbFile)), std::istreambuf_iterator<char>());
    CHECK(text.find("baseColorTexture") == std::string::npos);

    fs::remove_all(dir);
}
