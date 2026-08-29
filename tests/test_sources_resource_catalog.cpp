// Tests for src/sources/catalog.hpp/.cpp -- the real sources::Catalog
// object REFACTOR/RESOURCE_CATALOG.md describes: the one place that owns
// the texture tier order (literal -> listfile -> fuzzy same-basename pool)
// and tier 3's claim-and-remove pool state, replacing export_materials.cpp's
// old three-way branch with one Resolved<EncodedTexture> per slot.

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

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
    auto r = cat.texture(0, /*textureType=*/1, ctxFor((dir / "mymodel.m2").string()));
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
    // filterCandidatesForType has nothing to prefer between them.
    writeFile(dir / "mymodel_1111.png", kPng);
    std::vector<uint8_t> otherPng = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0xCD};
    writeFile(dir / "mymodel_2222.png", otherPng);

    Catalog cat(dir.string(), {}, "", "");
    auto r = cat.texture(0, /*textureType=*/1, ctxFor((dir / "mymodel.m2").string()));
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

    auto first = cat.texture(0, /*textureType=*/1, ctxFor(modelPath, /*slot=*/0));
    REQUIRE(first.found());
    CHECK(first.tier == ResolutionTier::FuzzySameBasenamePool);

    // A second, distinct slot (different textureSlotIndex) racing the same
    // now-depleted pool gets a genuine miss, not the same file reused.
    auto second = cat.texture(0, /*textureType=*/1, ctxFor(modelPath, /*slot=*/1));
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

    auto firstBatch = cat.texture(0, /*textureType=*/1, ctxFor(modelPath, /*slot=*/3));
    auto secondBatchSameSlot = cat.texture(0, /*textureType=*/1, ctxFor(modelPath, /*slot=*/3));
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
