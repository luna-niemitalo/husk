// Tests for src/sources/catalog.hpp/.cpp -- the real sources::Catalog
// object REFACTOR/RESOURCE_CATALOG.md describes: the one place that owns
// the texture tier order (literal -> listfile -> fuzzy same-basename pool)
// and tier 3's claim-and-remove pool state, replacing export_materials.cpp's
// old three-way branch with one Resolved<EncodedTexture> per slot.

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "../src/export_texture_resolution.hpp"
#include "../src/sources/catalog.hpp"

using husk::sources::Catalog;
using husk::sources::ResolutionTier;
using husk::sources::TextureModelContext;

namespace {

namespace fs = std::filesystem;

void writeFile(const fs::path& path, const std::vector<uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

const std::vector<uint8_t> kPng = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0xAB};

TextureModelContext ctxFor(const std::string& modelPath, uint16_t slot = 0) {
    TextureModelContext ctx;
    ctx.modelPath = modelPath;
    ctx.textureSlotIndex = slot;
    return ctx;
}

}  // namespace

TEST_CASE("Catalog::texture hits tier 1 (literal) when <texturesDir>/<fdid>.png exists") {
    auto dir = fs::temp_directory_path() / "husk-catalog-literal-hit";
    fs::create_directories(dir);
    writeFile(dir / "424242.png", kPng);

    Catalog cat(dir.string(), {}, "", "");
    auto r = cat.texture(424242, /*textureType=*/1, ctxFor((dir / "somemodel.m2").string()));
    REQUIRE(r.found());
    CHECK(r.tier == ResolutionTier::Literal);
    CHECK(r.value->bytes == kPng);
    CHECK(r.value->imageName == "424242");
    CHECK(r.alternates.empty());
}

TEST_CASE("Catalog::texture hits tier 2 (listfile) when tier 1 misses but the listfile names a real file") {
    auto root = fs::temp_directory_path() / "husk-catalog-listfile-hit";
    fs::create_directories(root / "world" / "goober");
    writeFile(root / "world" / "goober" / "bubble.png", kPng);

    std::unordered_map<uint32_t, std::string> listfile{{555, "world/goober/bubble.blp"}};
    Catalog cat(root.string(), listfile, root.string(), "");
    auto r = cat.texture(555, /*textureType=*/1, ctxFor((root / "somemodel.m2").string()));
    REQUIRE(r.found());
    CHECK(r.tier == ResolutionTier::Listfile);
    CHECK(r.value->bytes == kPng);
    CHECK(r.value->imageName == "bubble");
}

TEST_CASE("Catalog::texture hits tier 3 (fuzzy pool) claiming the sole type-compatible candidate") {
    auto dir = fs::temp_directory_path() / "husk-catalog-fuzzy-hit";
    fs::create_directories(dir);
    writeFile(dir / "mymodel_9999.png", kPng);

    Catalog cat(dir.string(), {}, "", "");
    // fdid == 0 -- a genuinely hardcoded slot, nothing for tiers 1/2 to try.
    // textureType 999 is deliberately not a real M2 texture type -- this
    // test is about generic tier-3 plumbing (claim-and-remove), not the
    // real per-type tag conjunction (export_texture_resolution.cpp's
    // textureTypeTagClauses), and "mymodel_9999" carries none of that
    // table's real vocabulary tokens on purpose.
    auto r = cat.texture(0, /*textureType=*/999, ctxFor((dir / "mymodel.m2").string()));
    REQUIRE(r.found());
    CHECK(r.tier == ResolutionTier::FuzzySameBasenamePool);
    CHECK(r.value->bytes == kPng);
    CHECK(r.value->matchedFilename == "mymodel_9999.png");
    CHECK(r.alternates.empty());
    CHECK(cat.remainingTexturePoolSize((dir / "mymodel.m2").string()) == 0);
}

TEST_CASE("Catalog::texture misses (with a reason) when no tier can answer at all") {
    auto dir = fs::temp_directory_path() / "husk-catalog-total-miss";
    fs::create_directories(dir);

    Catalog cat(dir.string(), {}, "", "");
    auto r = cat.texture(0, /*textureType=*/1, ctxFor((dir / "nothingnamedthis.m2").string()));
    CHECK_FALSE(r.found());
    CHECK(r.tier == ResolutionTier::FuzzySameBasenamePool);
    CHECK_FALSE(r.reason.empty());
}

TEST_CASE("Catalog::texture reports genuine ambiguity via Resolved<T>::alternates, not a variant") {
    auto dir = fs::temp_directory_path() / "husk-catalog-ambiguous";
    fs::create_directories(dir);
    // Two unrecognized-category candidates sharing the model's basename --
    // filterCandidatesForType has nothing to prefer between them. Real
    // textureType 999 (not a real M2 type, no tag-conjunction entry) --
    // this test is about the generic ambiguity/alternates plumbing.
    writeFile(dir / "mymodel_1111.png", kPng);
    std::vector<uint8_t> otherPng = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0xCD};
    writeFile(dir / "mymodel_2222.png", otherPng);

    Catalog cat(dir.string(), {}, "", "");
    auto r = cat.texture(0, /*textureType=*/999, ctxFor((dir / "mymodel.m2").string()));
    REQUIRE(r.found());
    CHECK(r.tier == ResolutionTier::FuzzySameBasenamePool);
    CHECK(r.alternates.size() == 2);
    // The chosen default (`value`) is also present in `alternates` --
    // RESOURCE_CATALOG.md's "a hit that knows it was a coin toss", not a
    // disjoint success shape excluding the picked default.
    bool chosenIsInAlternates = false;
    for (const auto& alt : r.alternates) {
        if (alt.filename == r.value->matchedFilename) chosenIsInAlternates = true;
    }
    CHECK(chosenIsInAlternates);
    // An ambiguous pool is never depleted -- both candidates stay available
    // for the next unresolved slot too.
    CHECK(cat.remainingTexturePoolSize((dir / "mymodel.m2").string()) == 2);
}

TEST_CASE("Catalog::texture depletes the pool across slots -- a claimed candidate isn't offered twice") {
    auto dir = fs::temp_directory_path() / "husk-catalog-pool-depletion";
    fs::create_directories(dir);
    writeFile(dir / "mymodel_5555.png", kPng);  // the pool's only real candidate

    Catalog cat(dir.string(), {}, "", "");
    std::string modelPath = (dir / "mymodel.m2").string();

    // textureType 999 -- not a real M2 type; this test is about pool
    // depletion, not the tag conjunction.
    auto first = cat.texture(0, /*textureType=*/999, ctxFor(modelPath, /*slot=*/0));
    REQUIRE(first.found());
    CHECK(first.tier == ResolutionTier::FuzzySameBasenamePool);

    // A second, distinct slot (different textureSlotIndex) racing the same
    // now-depleted pool gets a genuine miss, not the same file reused.
    auto second = cat.texture(0, /*textureType=*/999, ctxFor(modelPath, /*slot=*/1));
    CHECK_FALSE(second.found());
    CHECK(cat.remainingTexturePoolSize(modelPath) == 0);
}

TEST_CASE("Catalog::texture memoizes per (model, textureSlotIndex) -- two batches sharing one M2 texture "
          "index agree on the identical answer instead of each depleting the pool") {
    auto dir = fs::temp_directory_path() / "husk-catalog-slot-memoization";
    fs::create_directories(dir);
    writeFile(dir / "mymodel_7777.png", kPng);  // sole candidate

    Catalog cat(dir.string(), {}, "", "");
    std::string modelPath = (dir / "mymodel.m2").string();

    // textureType 999 -- not a real M2 type; this test is about slot
    // memoization, not the tag conjunction.
    auto firstBatch = cat.texture(0, /*textureType=*/999, ctxFor(modelPath, /*slot=*/3));
    auto secondBatchSameSlot = cat.texture(0, /*textureType=*/999, ctxFor(modelPath, /*slot=*/3));
    REQUIRE(firstBatch.found());
    REQUIRE(secondBatchSameSlot.found());
    CHECK(firstBatch.value->matchedFilename == secondBatchSameSlot.value->matchedFilename);
    // Only one claim actually happened against the pool -- if the second
    // call had re-run the claim, the pool would already be empty from the
    // first call and this would be a miss instead.
    CHECK(cat.remainingTexturePoolSize(modelPath) == 0);
}

TEST_CASE("Catalog excludes a pool candidate whose own trailing FileDataID is already one of this M2's "
          "own texture-array entries") {
    auto dir = fs::temp_directory_path() / "husk-catalog-own-fdid-exclusion";
    fs::create_directories(dir);
    writeFile(dir / "mymodel_3333.png", kPng);  // this model's own particle-sprite texture, fdid 3333

    Catalog cat(dir.string(), {}, "", "");
    TextureModelContext ctx = ctxFor((dir / "mymodel.m2").string());
    ctx.ownTextureFileDataIds = {3333};
    auto r = cat.texture(0, /*textureType=*/1, ctx);
    CHECK_FALSE(r.found());  // the only candidate was excluded, not offered as a guess
}

TEST_CASE("Catalog::registerPathOverride lets tier 2 resolve a fdid with no real listfile row, "
          "without mutating any listfile map the caller owns") {
    auto root = fs::temp_directory_path() / "husk-catalog-path-override";
    fs::create_directories(root / "item" / "objectcomponents");
    writeFile(root / "item" / "objectcomponents" / "recolor.png", kPng);

    std::unordered_map<uint32_t, std::string> callerListfile{{1, "unrelated/path.blp"}};
    Catalog cat(root.string(), callerListfile, root.string(), "");
    cat.registerPathOverride(9001, "item/objectcomponents/recolor.blp");

    auto r = cat.texture(9001, /*textureType=*/2, ctxFor((root / "mymodel.m2").string()));
    REQUIRE(r.found());
    CHECK(r.tier == ResolutionTier::Listfile);
    CHECK(r.value->bytes == kPng);
    // The caller's own map is untouched: a caller may share it with other
    // consumers, so an override must never be written back into it.
    CHECK(callerListfile.size() == 1);
    CHECK(callerListfile.count(9001) == 0);
}

TEST_CASE("Catalog::describe() names every resolved slot -- I4's ledger, a read operation") {
    auto dir = fs::temp_directory_path() / "husk-catalog-describe";
    fs::create_directories(dir);
    writeFile(dir / "111.png", kPng);

    Catalog cat(dir.string(), {}, "", "");
    cat.texture(111, /*textureType=*/1, ctxFor((dir / "mymodel.m2").string(), /*slot=*/0));
    cat.texture(0, /*textureType=*/1, ctxFor((dir / "mymodel.m2").string(), /*slot=*/1));

    auto ledger = cat.describe();
    CHECK(ledger.find("fdid=111") != std::string::npos);
    CHECK(ledger.find("literal") != std::string::npos);
    CHECK(ledger.find("HIT") != std::string::npos);
    CHECK(ledger.find("MISS") != std::string::npos);
}

// Tag-conjunction pool admission + per-type query, and the `_hd` hard
// partition, below.

TEST_CASE("extractTextureTags finds real vocabulary tokens run together with no separator -- the "
          "exact real-corpus shape (e.g. \"...nakedtorsoskin00_105_hd.blp\") the old startswith-"
          "basename pool gate could never reach at all") {
    auto tags = husk::commands::extractTextureTags("bloodelffemalenakedtorsoskin00_105_hd");
    CHECK(tags.count("naked") == 1);
    CHECK(tags.count("torso") == 1);
    CHECK(tags.count("skin") == 1);
    // No spurious matches -- a file with none of the vocabulary words
    // reports an empty set, not a false positive.
    CHECK(husk::commands::extractTextureTags("tempmonktexture").empty());
}

TEST_CASE("filenameCarriesHdToken requires a real delimited '_hd' token, not a bare substring") {
    CHECK(husk::commands::filenameCarriesHdToken("bloodelffemale_hd"));
    CHECK(husk::commands::filenameCarriesHdToken("bloodelffemaleskin00_00_hd"));
    CHECK_FALSE(husk::commands::filenameCarriesHdToken("bloodelffemale"));
    // "hd" only as a substring inside a longer token (not its own
    // delimited piece) must not count -- otherwise an ordinary word could
    // false-positive the `_hd` partition.
    CHECK_FALSE(husk::commands::filenameCarriesHdToken("shadow"));
}

TEST_CASE("filterCandidatesByTextureTag: type 20 (char_jewelry) requires 'jewelry' AND 'color' "
          "together, excluding a bare 'jewelry' overlay -- reproduces the real "
          "bloodelffemale_hd distinction (candidateCategoryTypes' own doc comment) that a bare "
          "'jewelry' tag alone can't make") {
    std::vector<std::filesystem::path> files = {
        "jewtest_jewelry_color_1000.png",  // real jewelry mesh texture -- both tags
        "jewtest_body_jewelry_2000.png",   // skin/skin_extra overlay -- "jewelry" only
    };
    auto matching = husk::commands::filterCandidatesByTextureTag(files, /*textureType=*/20);
    REQUIRE(matching.has_value());
    REQUIRE(matching->size() == 1);
    CHECK(matching->front() == "jewtest_jewelry_color_1000.png");
}

TEST_CASE("filterCandidatesByTextureTag returns nullopt for a texture type with no established "
          "tag mapping -- object_skin(2) and every non-character replaceable type stay on the "
          "older filterCandidatesForType path, never silently admit everything") {
    std::vector<std::filesystem::path> files = {"anything_at_all.png"};
    CHECK_FALSE(husk::commands::filterCandidatesByTextureTag(files, /*textureType=*/2).has_value());
    CHECK_FALSE(husk::commands::filterCandidatesByTextureTag(files, /*textureType=*/11).has_value());
}

TEST_CASE("Catalog::texture: a tag-carrying candidate with no name relation to the model "
          "at all is still admitted into the pool and resolves a taggable hardcoded slot -- the "
          "exact real gap this project's own tag-conjunction work closed (a real bloodelffemale_hd hairstyle "
          "file, \"scalpupperhair00_08_hd\", shares no basename with the model whatsoever)") {
    auto dir = fs::temp_directory_path() / "husk-catalog-tag-admits-non-basename";
    fs::create_directories(dir);
    // No "elfmale" prefix anywhere in this filename -- the old
    // startswith(basename) gate could never have admitted this file.
    writeFile(dir / "scalpupperhair00_08_hd.png", kPng);

    Catalog cat(dir.string(), {}, "", "");
    TextureModelContext ctx = ctxFor((dir / "elfmale_hd.m2").string());
    auto r = cat.texture(0, /*textureType=*/6, ctx);  // 6 = char_hair
    REQUIRE(r.found());
    CHECK(r.tier == ResolutionTier::FuzzySameBasenamePool);
    CHECK(r.value->matchedFilename == "scalpupperhair00_08_hd.png");
}

TEST_CASE("Catalog::texture: a non-'_hd' model never draws '_hd' art, even when both "
          "share the model's own basename and both are otherwise tag-compatible -- the real "
          "bloodelffemale.m2 bug (three distinct hardcoded slots all defaulting to the identical "
          "'_hd' file, since 'bloodelffemale' is a string-prefix of every real "
          "'bloodelffemale_hd_*' filename)") {
    auto dir = fs::temp_directory_path() / "husk-catalog-hd-partition-excludes-hd";
    fs::create_directories(dir);
    writeFile(dir / "elfmale_skin_color_1000_hd.png", kPng);     // "_hd" art -- must not apply
    std::vector<uint8_t> otherPng = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0xCD};
    writeFile(dir / "elfmale_skin_color_1001.png", otherPng);    // real non-"_hd" art

    Catalog cat(dir.string(), {}, "", "");
    // "elfmale" itself carries no "_hd" token -- a non-"_hd" model.
    auto r = cat.texture(0, /*textureType=*/1, ctxFor((dir / "elfmale.m2").string()));  // 1 = skin
    REQUIRE(r.found());
    // Sole match, not ambiguous -- the "_hd" candidate was excluded at the
    // pool itself, never offered as an alternate to guess between.
    CHECK(r.alternates.empty());
    CHECK(r.value->matchedFilename == "elfmale_skin_color_1001.png");
}

TEST_CASE("Catalog::texture: an '_hd' model never draws non-'_hd' art either -- the "
          "hard partition cuts both directions, not just the poisoned-non-HD-pool direction") {
    auto dir = fs::temp_directory_path() / "husk-catalog-hd-partition-excludes-non-hd";
    fs::create_directories(dir);
    writeFile(dir / "elfmale_hd_skin_color_1000.png", kPng);  // real "_hd" art (model's own basename)
    std::vector<uint8_t> otherPng = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0xCD};
    // Same "skin"+"color" tags, admitted via the tag path -- but no "_hd"
    // token anywhere, so it's non-"_hd" art and must not apply here.
    writeFile(dir / "othermodel_skin_color_1001.png", otherPng);

    Catalog cat(dir.string(), {}, "", "");
    auto r = cat.texture(0, /*textureType=*/1, ctxFor((dir / "elfmale_hd.m2").string()));  // 1 = skin
    REQUIRE(r.found());
    CHECK(r.alternates.empty());
    CHECK(r.value->matchedFilename == "elfmale_hd_skin_color_1000.png");
}
