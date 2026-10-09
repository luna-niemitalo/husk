// Tests for src/sources/catalog.hpp/.cpp -- the real sources::Catalog
// object REFACTOR/RESOURCE_CATALOG.md describes: the one place that owns
// the texture tier order (literal -> listfile -> fuzzy same-basename pool)
// and tier 3's claim-and-remove pool state, replacing export_materials.cpp's
// old three-way branch with one Resolved<EncodedTexture> per slot.

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>

#include "../src/export_materials.hpp"
#include "../src/export_texture_resolution.hpp"
#include "../src/listfile_index.hpp"
#include "../src/sources/catalog.hpp"

using husk::sources::Catalog;
using husk::sources::CharacterTextureContext;
using husk::sources::ResolutionTier;
using husk::sources::TextureModelContext;

namespace {

// Catalog holds its listfile by reference (catalog.hpp), so it must outlive
// every Catalog below -- a temporary passed inline would dangle.
const husk::EmptyListfileIndex kNoListfile;

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

    Catalog cat(dir.string(), kNoListfile, "", "");
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
    husk::MapListfileIndex listfileIndex(listfile);
    Catalog cat(root.string(), listfileIndex, root.string(), "");
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

    Catalog cat(dir.string(), kNoListfile, "", "");
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

    Catalog cat(dir.string(), kNoListfile, "", "");
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

    Catalog cat(dir.string(), kNoListfile, "", "");
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

    Catalog cat(dir.string(), kNoListfile, "", "");
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

    Catalog cat(dir.string(), kNoListfile, "", "");
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

    Catalog cat(dir.string(), kNoListfile, "", "");
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
    husk::MapListfileIndex callerListfileIndex(callerListfile);
    Catalog cat(root.string(), callerListfileIndex, root.string(), "");
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

    Catalog cat(dir.string(), kNoListfile, "", "");
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

    Catalog cat(dir.string(), kNoListfile, "", "");
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

    Catalog cat(dir.string(), kNoListfile, "", "");
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

    Catalog cat(dir.string(), kNoListfile, "", "");
    auto r = cat.texture(0, /*textureType=*/1, ctxFor((dir / "elfmale_hd.m2").string()));  // 1 = skin
    REQUIRE(r.found());
    CHECK(r.alternates.empty());
    CHECK(r.value->matchedFilename == "elfmale_hd_skin_color_1000.png");
}

TEST_CASE("Catalog::texture: an untagged texture type (no textureTypeTagClauses entry) never draws "
          "a tag-only-admitted candidate into its own query -- the pool-scan admission is widened "
          "for every type, including ones with no real tag clause to justify it, so the per-type "
          "query must itself restrict back down to the original startswith(basename) admission for "
          "those types (real bloodelffemale_hd regression: object_skin/type 2 went from 3 candidates "
          "to 386 before this restriction existed, purely from inheriting a wider gate a different "
          "type's own clause earned)") {
    auto dir = fs::temp_directory_path() / "husk-catalog-untagged-type-narrow-gate";
    fs::create_directories(dir);
    // Narrow-admissible: starts with the model's own basename.
    writeFile(dir / "mymodel_1000.png", kPng);
    // Wide-only: shares no basename with the model at all, admitted into the
    // pool solely by carrying a real vocabulary tag ("skin") -- exactly the
    // shape scanFuzzyTexturePoolForBasename's OR-widened admission exists
    // for, and exactly what an untagged type must not be offered.
    std::vector<uint8_t> otherPng = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0xCD};
    writeFile(dir / "somethingelse_skin_2000.png", otherPng);

    Catalog cat(dir.string(), kNoListfile, "", "");
    // textureType 2 = object_skin, real M2 type with no textureTypeTagClauses
    // entry (confirmed unreachable by any filename tag, see
    // export_texture_resolution.cpp's own textureTypeTagClauses doc comment).
    auto r = cat.texture(0, /*textureType=*/2, ctxFor((dir / "mymodel.m2").string()));
    REQUIRE(r.found());
    CHECK(r.tier == ResolutionTier::FuzzySameBasenamePool);
    // Sole match, not ambiguous -- the wide-only candidate was never even
    // offered to this query, so there was nothing to disambiguate.
    CHECK(r.alternates.empty());
    CHECK(r.value->matchedFilename == "mymodel_1000.png");
}

TEST_CASE("Catalog::texture: a *taggable* texture type still draws a tag-only-admitted candidate "
          "with no basename relation at all -- the narrow-gate restriction above is type-specific, "
          "not a blanket revert of the pool-scan widening; tagged types keep their real recall gain") {
    auto dir = fs::temp_directory_path() / "husk-catalog-tagged-type-keeps-wide-gate";
    fs::create_directories(dir);
    writeFile(dir / "mymodel_1000.png", kPng);  // narrow-admissible, but no "skin"/"face"/etc. tag
    std::vector<uint8_t> otherPng = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0xCD};
    writeFile(dir / "somethingelse_skin_2000.png", otherPng);  // wide-only, real "skin" tag

    Catalog cat(dir.string(), kNoListfile, "", "");
    // textureType 1 = skin, has a real textureTypeTagClauses entry -- only
    // the "skin"-tagged file satisfies the query; "mymodel_1000" carries no
    // vocabulary tag at all and is correctly excluded by the tag query
    // itself, not by the narrow-gate restriction (which doesn't apply here).
    auto r = cat.texture(0, /*textureType=*/1, ctxFor((dir / "mymodel.m2").string()));
    REQUIRE(r.found());
    CHECK(r.tier == ResolutionTier::FuzzySameBasenamePool);
    CHECK(r.alternates.empty());
    CHECK(r.value->matchedFilename == "somethingelse_skin_2000.png");
}

// The DB2-character tier -- opt-in via setCharacterTextureContext, ranked
// between tier 2 (listfile) and tier 3 (fuzzy pool). Below: the clean no-op
// (feature unused), the real fires-correctly case (reproducing
// bloodelffemale_hd's own jewelry slot), and each of the three independent
// miss conditions (no single-layer target for this type, no resolved
// choice feeding that target, resolved but with fileDataId 0).

TEST_CASE("Catalog::texture: setCharacterTextureContext never called is a clean no-op -- "
          "every model/invocation without --db2-dir/--dbd-dir behaves exactly as before this "
          "tier existed, falling straight through to the fuzzy pool") {
    auto dir = fs::temp_directory_path() / "husk-catalog-db2-character-unset";
    fs::create_directories(dir);
    writeFile(dir / "mymodel_9999.png", kPng);

    Catalog cat(dir.string(), kNoListfile, "", "");
    auto r = cat.texture(0, /*textureType=*/999, ctxFor((dir / "mymodel.m2").string()));
    REQUIRE(r.found());
    CHECK(r.tier == ResolutionTier::FuzzySameBasenamePool);
}

TEST_CASE("Catalog::texture: an explicitly-set but empty CharacterTextureContext is also a "
          "clean no-op -- absence of a matching entry, not the absence of the struct itself, "
          "is what makes this tier miss") {
    auto dir = fs::temp_directory_path() / "husk-catalog-db2-character-empty";
    fs::create_directories(dir);
    writeFile(dir / "mymodel_9999.png", kPng);

    Catalog cat(dir.string(), kNoListfile, "", "");
    cat.setCharacterTextureContext(CharacterTextureContext{});
    auto r = cat.texture(0, /*textureType=*/999, ctxFor((dir / "mymodel.m2").string()));
    REQUIRE(r.found());
    CHECK(r.tier == ResolutionTier::FuzzySameBasenamePool);
}

TEST_CASE("Catalog::texture: the DB2-character tier fires when all three conditions hold, "
          "reads the DB2-derived FileDataID via the normal literal tier, and outranks the fuzzy "
          "pool even when the pool also has a type-compatible candidate -- reproduces the real "
          "bloodelffemale_hd jewelry-slot delta (fuzzy pool's arbitrary pick happened to be the "
          "same file, target 38 -> FileDataID 3613861)") {
    auto dir = fs::temp_directory_path() / "husk-catalog-db2-character-fires";
    fs::create_directories(dir);
    writeFile(dir / "3613861.png", kPng);  // the DB2-derived fdid, resolved via tier 1 (literal)
    std::vector<uint8_t> poolPng = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0xCD};
    writeFile(dir / "elfmale_jewelry_color_9999999.png", poolPng);  // a real, distinct fuzzy-pool candidate

    Catalog cat(dir.string(), kNoListfile, "", "");
    CharacterTextureContext ctx;
    ctx.singleLayerTargetByTextureType[20] = 38;  // type 20 = char_jewelry, real target from Q1
    ctx.fileDataIdByTarget[38] = 3613861;
    cat.setCharacterTextureContext(ctx);

    auto r = cat.texture(0, /*textureType=*/20, ctxFor((dir / "elfmale_hd.m2").string()));
    REQUIRE(r.found());
    CHECK(r.tier == ResolutionTier::Db2Character);
    CHECK(r.value->bytes == kPng);
    CHECK(r.value->imageName == "3613861");
    // The fuzzy pool's own candidate was never touched -- this tier answers
    // via tier 1/2 machinery, not tier 3, so the pool stays fully intact for
    // whatever slot actually needs it.
    CHECK(cat.remainingTexturePoolSize((dir / "elfmale_hd.m2").string()) == 0);
}

TEST_CASE("Catalog::texture: DB2-character tier misses (falls through to the fuzzy pool) when "
          "the texture type has no single-layer target at all -- the real multi-layer-type case "
          "(e.g. skin, or eyes on a layout with 2 live targets), never a hardcoded type list") {
    auto dir = fs::temp_directory_path() / "husk-catalog-db2-character-miss-no-target";
    fs::create_directories(dir);
    writeFile(dir / "mymodel_1111.png", kPng);

    Catalog cat(dir.string(), kNoListfile, "", "");
    CharacterTextureContext ctx;
    ctx.fileDataIdByTarget[38] = 3613861;  // a resolved material exists, but for a different target
    cat.setCharacterTextureContext(ctx);

    auto r = cat.texture(0, /*textureType=*/999, ctxFor((dir / "mymodel.m2").string()));
    REQUIRE(r.found());
    CHECK(r.tier == ResolutionTier::FuzzySameBasenamePool);  // never Db2Character
}

TEST_CASE("Catalog::texture: DB2-character tier misses when the single-layer target has no "
          "resolved customization choice feeding it -- e.g. Hair Color/Blindfold's real "
          "'materials: null' default-choice gap") {
    auto dir = fs::temp_directory_path() / "husk-catalog-db2-character-miss-no-choice";
    fs::create_directories(dir);
    writeFile(dir / "mymodel_1111.png", kPng);

    Catalog cat(dir.string(), kNoListfile, "", "");
    CharacterTextureContext ctx;
    // textureType 999 -- not a real M2 type, no tag-conjunction clause, so
    // the fuzzy-pool fallback below matches via the plain basename-prefix
    // path; this test is about the DB2-tier's own miss condition, not tier
    // 3's tag conjunction.
    ctx.singleLayerTargetByTextureType[999] = 10;  // a target -- but no fileDataIdByTarget entry at all
    cat.setCharacterTextureContext(ctx);

    auto r = cat.texture(0, /*textureType=*/999, ctxFor((dir / "mymodel.m2").string()));
    REQUIRE(r.found());
    CHECK(r.tier == ResolutionTier::FuzzySameBasenamePool);
}

TEST_CASE("Catalog::texture: DB2-character tier misses when the resolved material carries "
          "FileDataID 0 -- a real material element exists, but texturefiledata.db2 didn't "
          "resolve it, distinct from 'no material at all'") {
    auto dir = fs::temp_directory_path() / "husk-catalog-db2-character-miss-fdid-zero";
    fs::create_directories(dir);
    writeFile(dir / "mymodel_1111.png", kPng);

    Catalog cat(dir.string(), kNoListfile, "", "");
    CharacterTextureContext ctx;  // textureType 999 -- see the previous test's own comment
    ctx.singleLayerTargetByTextureType[999] = 10;
    ctx.fileDataIdByTarget[10] = 0;  // resolved element, unresolved FileDataID
    cat.setCharacterTextureContext(ctx);

    auto r = cat.texture(0, /*textureType=*/999, ctxFor((dir / "mymodel.m2").string()));
    REQUIRE(r.found());
    CHECK(r.tier == ResolutionTier::FuzzySameBasenamePool);
}

TEST_CASE("Catalog::texture: the DB2-character tier is never consulted when the slot's own M2 "
          "fdid already resolves via tier 1/2 -- it sits strictly between listfile and the fuzzy "
          "pool, not ahead of the deterministic tiers") {
    auto dir = fs::temp_directory_path() / "husk-catalog-db2-character-tier1-wins";
    fs::create_directories(dir);
    writeFile(dir / "424242.png", kPng);  // the slot's own real fdid, resolves via tier 1

    Catalog cat(dir.string(), kNoListfile, "", "");
    CharacterTextureContext ctx;
    ctx.singleLayerTargetByTextureType[1] = 5;
    ctx.fileDataIdByTarget[5] = 999999;  // would resolve to a different file if consulted at all
    cat.setCharacterTextureContext(ctx);

    auto r = cat.texture(424242, /*textureType=*/1, ctxFor((dir / "mymodel.m2").string()));
    REQUIRE(r.found());
    CHECK(r.tier == ResolutionTier::Literal);
}

// buildCharacterTextureContext (export_materials.hpp/.cpp) -- the reduction
// that feeds setCharacterTextureContext above, built from the same
// gltf::Skeleton fields attachCharTextureLayout/attachCustomizationChoices
// already populate (cmd_export.cpp), never a second DB2 load of its own.

TEST_CASE("buildCharacterTextureContext: an unpopulated skeleton (no DB2 data at all) reduces to "
          "two empty maps -- the clean no-op every other DB2-driven enrichment in this project "
          "guarantees") {
    husk::gltf::Skeleton skeleton;
    auto ctx = husk::commands::buildCharacterTextureContext(skeleton);
    CHECK(ctx.singleLayerTargetByTextureType.empty());
    CHECK(ctx.fileDataIdByTarget.empty());
}

TEST_CASE("buildCharacterTextureContext: a textureType with exactly one real ChrModelTextureLayer "
          "row maps to that row's own target; a textureType with more than one (real skin/type-1 "
          "shape) is excluded entirely, never picking an arbitrary one of the several targets") {
    husk::gltf::Skeleton skeleton;
    husk::gltf::Skeleton::CharTextureLayout layout;
    layout.textureLayers.push_back({1, /*textureType=*/6, 0, 0, 0, 0, /*chrModelTextureTargetId=*/10});
    layout.textureLayers.push_back({2, /*textureType=*/1, 0, 0, 0, 0, /*chrModelTextureTargetId=*/1});
    layout.textureLayers.push_back({3, /*textureType=*/1, 0, 0, 0, 0, /*chrModelTextureTargetId=*/13});
    skeleton.charTextureLayout = layout;

    auto ctx = husk::commands::buildCharacterTextureContext(skeleton);
    REQUIRE(ctx.singleLayerTargetByTextureType.count(6) == 1);
    CHECK(ctx.singleLayerTargetByTextureType.at(6) == 10);
    CHECK(ctx.singleLayerTargetByTextureType.count(1) == 0);  // 2 rows -- excluded, not guessed at
}

TEST_CASE("buildCharacterTextureContext: enabledMaterials reduces straight to a target -> "
          "fileDataId map, fileDataId 0 kept (not dropped) since it's a real, distinct miss for "
          "the DB2-character tier to report") {
    husk::gltf::Skeleton skeleton;
    skeleton.enabledMaterials.push_back({/*choiceId=*/100, /*chrModelTextureTargetId=*/38,
                                          /*materialResourcesId=*/500, /*fileDataId=*/3613861});
    skeleton.enabledMaterials.push_back({/*choiceId=*/101, /*chrModelTextureTargetId=*/10,
                                          /*materialResourcesId=*/501, /*fileDataId=*/0});

    auto ctx = husk::commands::buildCharacterTextureContext(skeleton);
    REQUIRE(ctx.fileDataIdByTarget.count(38) == 1);
    CHECK(ctx.fileDataIdByTarget.at(38) == 3613861);
    REQUIRE(ctx.fileDataIdByTarget.count(10) == 1);
    CHECK(ctx.fileDataIdByTarget.at(10) == 0);
}
