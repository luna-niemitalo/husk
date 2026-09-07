// Structural convergence proof for canon::assembleDefinition
// (canon_definition_builder.hpp) against chrcustomization::namedChoicesForModel
// (chrcustomization_db2.cpp) -- the real DB2 traversal export_extras.cpp's
// own customizationOptions-building loop also drives -- REFACTOR/README.md
// stage 3's gate. assembleDefinition reuses namedChoicesForModel directly
// rather than re-walking Data itself, so most of these checks are against
// facts derived straight from chrcustomization::Data (the same already-loaded
// structure both assembleDefinition and export_extras.cpp consume), not
// against namedChoicesForModel's own output -- same "derive independently,
// even if that duplicates a little logic" rule test_canon_material_convergence.cpp
// already establishes.

#include <algorithm>
#include <doctest/doctest.h>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>

#include "canon_definition_builder.hpp"
#include "test_data_paths.hpp"

using namespace husk;

namespace {

canon::Definition assemble(const chrcustomization::Data& data, uint32_t chrModelId) {
    std::ostringstream err;
    return canon::assembleDefinition(data, chrModelId, err);
}

}  // namespace

TEST_CASE("canon::assembleDefinition converges with chrcustomization::Data against real local "
          "ChrCustomizationOption/Choice/Category data (test_data/db2, reference/WoWDBDefs)" *
          doctest::skip(test::testDbdDir().empty())) {
    std::ostringstream err;
    std::optional<chrcustomization::Data> data = chrcustomization::load("test_data/db2", test::testDbdDir(), err);
    REQUIRE(data.has_value());
    REQUIRE_FALSE(data->options.empty());
    REQUIRE_FALSE(data->choices.empty());

    // Real ChrModelID 20 (bloodelffemale_hd, per this project's own history
    // of cross-checking this exact ID against these exact real files via
    // `husk db2-export` + sqlite3 -- see CLAUDE_HISTORY.md) -- any ChrModelID
    // present in the real local data would do, since every check below is
    // derived from `data` itself, not from a hardcoded expected shape.
    const uint32_t chrModelId = 20;

    // Independently derive, straight from chrcustomization::Data, which
    // options/choices should survive into the Definition -- namedChoicesForModel
    // (and therefore assembleDefinition) drops any option with zero real
    // choices, so "has choices" is part of the independent expectation too,
    // not just "chrModelId matches".
    std::map<uint32_t, const chrcustomization::Option*> expectedOptionsById;
    for (const auto& opt : data->options) {
        if (opt.chrModelId != chrModelId) continue;
        bool hasChoice = std::any_of(data->choices.begin(), data->choices.end(),
                                      [&](const chrcustomization::Choice& c) { return c.optionId == opt.id; });
        if (hasChoice) expectedOptionsById[opt.id] = &opt;
    }
    REQUIRE_FALSE(expectedOptionsById.empty());

    canon::Definition result = assemble(*data, chrModelId);
    CHECK(result.chrModelId == chrModelId);
    REQUIRE(result.options.size() == expectedOptionsById.size());

    // One CHECK per kind of fact verified, not one per option/choice -- a
    // real customization menu runs to dozens of options and hundreds of
    // choices, and asserting per-item-per-field multiplies this test's
    // assertion count by that corpus size for no added coverage (every
    // option and every choice is still examined; only the assertion *count*
    // changes). Same aggregate-then-diagnose discipline
    // test_canon_skeleton_convergence.cpp's per-joint loop already
    // establishes. A structural precondition (e.g. "id is a Db2Row") that
    // would crash on `std::get` if false is guarded with `continue` instead
    // of `REQUIRE`, so one option's bad shape doesn't abort inspection of
    // the rest of the corpus.

    // Definition::options must be in real optionOrderIndex order (ties
    // broken by id) -- canon_definition_builder.hpp's own deliberate
    // divergence from export_extras.cpp's insertion-order-preserving loop.
    std::optional<size_t> orderMismatch;
    for (size_t i = 1; i < result.options.size(); ++i) {
        const auto& prev = result.options[i - 1];
        const auto& cur = result.options[i];
        bool ordered = prev.orderIndex < cur.orderIndex ||
                       (prev.orderIndex == cur.orderIndex &&
                        std::get<canon::Db2Row>(prev.ref.id).row < std::get<canon::Db2Row>(cur.ref.id).row);
        if (!ordered && !orderMismatch) orderMismatch = i;
    }
    INFO("first option index breaking real orderIndex ordering (if any): ", orderMismatch.value_or(-1));
    CHECK(!orderMismatch.has_value());

    std::optional<size_t> refIdKindMismatch, tableNameMismatch, unknownOptionRow, orderIndexMismatch,
        nameFieldMismatch, categoryRefKindMismatch, categoryRowMismatch, categoryOrderMismatch,
        categoryNameMismatch, choicesSizeMismatch;
    std::optional<std::pair<size_t, size_t>> choiceRefKindMismatch, choiceRowMismatch, choiceOrderMismatch,
        choiceNameMismatch;

    std::set<uint32_t> seenOptionIds;
    for (size_t oi = 0; oi < result.options.size(); ++oi) {
        const auto& opt = result.options[oi];
        if (!std::holds_alternative<canon::Db2Row>(opt.ref.id)) {
            if (!refIdKindMismatch) refIdKindMismatch = oi;
            continue;
        }
        auto& row = std::get<canon::Db2Row>(opt.ref.id);
        if (row.table != "ChrCustomizationOption" && !tableNameMismatch) tableNameMismatch = oi;
        auto it = expectedOptionsById.find(row.row);
        if (it == expectedOptionsById.end()) {
            if (!unknownOptionRow) unknownOptionRow = oi;
            continue;
        }
        seenOptionIds.insert(row.row);
        const chrcustomization::Option& expectedOpt = *it->second;

        if (opt.orderIndex != expectedOpt.orderIndex && !orderIndexMismatch) orderIndexMismatch = oi;
        bool nameMatches = expectedOpt.name.empty()
                                ? (opt.ref.name.empty() && opt.ref.source == canon::NameSource::None)
                                : (opt.ref.name == expectedOpt.name && opt.ref.source == canon::NameSource::Db2);
        if (!nameMatches && !nameFieldMismatch) nameFieldMismatch = oi;

        // Category: resolved independently against data->categories, same
        // join namedChoicesForModel performs internally -- 0/unresolved
        // must surface as no identity/name, never fabricated.
        const chrcustomization::Category* expectedCategory = nullptr;
        if (expectedOpt.categoryId != 0) {
            for (const auto& c : data->categories) {
                if (c.id == expectedOpt.categoryId) {
                    expectedCategory = &c;
                    break;
                }
            }
        }
        if (!std::holds_alternative<canon::Db2Row>(opt.category.ref.id)) {
            if (!categoryRefKindMismatch) categoryRefKindMismatch = oi;
        } else if (expectedCategory != nullptr) {
            bool categoryMatches =
                std::get<canon::Db2Row>(opt.category.ref.id).row == expectedCategory->id &&
                opt.category.orderIndex == expectedCategory->orderIndex;
            if (!categoryMatches && !categoryRowMismatch) categoryRowMismatch = oi;
            if (opt.category.orderIndex != expectedCategory->orderIndex && !categoryOrderMismatch) {
                categoryOrderMismatch = oi;
            }
            bool categoryNameMatches = expectedCategory->name.empty()
                                            ? (opt.category.ref.name.empty() &&
                                               opt.category.ref.source == canon::NameSource::None)
                                            : (opt.category.ref.name == expectedCategory->name &&
                                               opt.category.ref.source == canon::NameSource::Db2);
            if (!categoryNameMatches && !categoryNameMismatch) categoryNameMismatch = oi;
        } else {
            bool noCategoryMatches = std::get<canon::Db2Row>(opt.category.ref.id).row == 0 &&
                                       opt.category.ref.name.empty() &&
                                       opt.category.ref.source == canon::NameSource::None;
            if (!noCategoryMatches && !categoryRowMismatch) categoryRowMismatch = oi;
        }

        // Choices: independently gathered from data->choices, sorted the
        // same deterministic way assembleDefinition itself must sort them.
        std::vector<const chrcustomization::Choice*> expectedChoices;
        for (const auto& c : data->choices) {
            if (c.optionId == expectedOpt.id) expectedChoices.push_back(&c);
        }
        std::sort(expectedChoices.begin(), expectedChoices.end(),
                  [](const chrcustomization::Choice* a, const chrcustomization::Choice* b) {
                      if (a->orderIndex != b->orderIndex) return a->orderIndex < b->orderIndex;
                      return a->id < b->id;
                  });
        if (opt.choices.size() != expectedChoices.size()) {
            if (!choicesSizeMismatch) choicesSizeMismatch = oi;
            continue;
        }
        for (size_t ci = 0; ci < opt.choices.size(); ++ci) {
            const auto& choice = opt.choices[ci];
            const auto& expectedChoice = *expectedChoices[ci];
            if (!std::holds_alternative<canon::Db2Row>(choice.ref.id)) {
                if (!choiceRefKindMismatch) choiceRefKindMismatch = {oi, ci};
                continue;
            }
            if (std::get<canon::Db2Row>(choice.ref.id).row != expectedChoice.id && !choiceRowMismatch) {
                choiceRowMismatch = {oi, ci};
            }
            if (choice.orderIndex != expectedChoice.orderIndex && !choiceOrderMismatch) {
                choiceOrderMismatch = {oi, ci};
            }
            bool choiceNameMatches =
                expectedChoice.name.empty()
                    ? (choice.ref.name.empty() && choice.ref.source == canon::NameSource::None)
                    : (choice.ref.name == expectedChoice.name && choice.ref.source == canon::NameSource::Db2);
            if (!choiceNameMatches && !choiceNameMismatch) choiceNameMismatch = {oi, ci};
        }
    }

    INFO("first option index with a ref.id that isn't a Db2Row (if any): ", refIdKindMismatch.value_or(-1));
    CHECK(!refIdKindMismatch.has_value());
    INFO("first option index with a wrong Db2Row.table (if any): ", tableNameMismatch.value_or(-1));
    CHECK(!tableNameMismatch.has_value());
    INFO("first option index whose row doesn't match any expected option (if any): ",
         unknownOptionRow.value_or(-1));
    CHECK(!unknownOptionRow.has_value());
    INFO("first option index with a mismatched orderIndex (if any): ", orderIndexMismatch.value_or(-1));
    CHECK(!orderIndexMismatch.has_value());
    INFO("first option index with a mismatched name/source (if any): ", nameFieldMismatch.value_or(-1));
    CHECK(!nameFieldMismatch.has_value());
    INFO("first option index whose category.ref.id isn't a Db2Row (if any): ",
         categoryRefKindMismatch.value_or(-1));
    CHECK(!categoryRefKindMismatch.has_value());
    INFO("first option index with a mismatched category row/identity (if any): ",
         categoryRowMismatch.value_or(-1));
    CHECK(!categoryRowMismatch.has_value());
    INFO("first option index with a mismatched category orderIndex (if any): ",
         categoryOrderMismatch.value_or(-1));
    CHECK(!categoryOrderMismatch.has_value());
    INFO("first option index with a mismatched category name/source (if any): ",
         categoryNameMismatch.value_or(-1));
    CHECK(!categoryNameMismatch.has_value());
    INFO("first option index with a mismatched choices count (if any): ", choicesSizeMismatch.value_or(-1));
    CHECK(!choicesSizeMismatch.has_value());
    INFO("first (option, choice) index whose ref.id isn't a Db2Row (if any): ",
         choiceRefKindMismatch.has_value()
             ? ("(" + std::to_string(choiceRefKindMismatch->first) + ", " +
                std::to_string(choiceRefKindMismatch->second) + ")")
             : std::string("none"));
    CHECK(!choiceRefKindMismatch.has_value());
    INFO("first (option, choice) index with a mismatched row id (if any): ",
         choiceRowMismatch.has_value()
             ? ("(" + std::to_string(choiceRowMismatch->first) + ", " +
                std::to_string(choiceRowMismatch->second) + ")")
             : std::string("none"));
    CHECK(!choiceRowMismatch.has_value());
    INFO("first (option, choice) index with a mismatched orderIndex (if any): ",
         choiceOrderMismatch.has_value()
             ? ("(" + std::to_string(choiceOrderMismatch->first) + ", " +
                std::to_string(choiceOrderMismatch->second) + ")")
             : std::string("none"));
    CHECK(!choiceOrderMismatch.has_value());
    INFO("first (option, choice) index with a mismatched name/source (if any): ",
         choiceNameMismatch.has_value()
             ? ("(" + std::to_string(choiceNameMismatch->first) + ", " +
                std::to_string(choiceNameMismatch->second) + ")")
             : std::string("none"));
    CHECK(!choiceNameMismatch.has_value());
    CHECK(seenOptionIds.size() == expectedOptionsById.size());
}

TEST_CASE("canon::assembleDefinition: empty Data (options/choices not both populated) yields an "
          "empty Definition, not an error -- mirrors namedChoicesForModel's own real "
          "\"tables genuinely weren't fetched locally\" behavior") {
    chrcustomization::Data emptyData;
    canon::Definition result = assemble(emptyData, 42);
    CHECK(result.chrModelId == 42);
    CHECK(result.options.empty());

    chrcustomization::Data onlyOptions;
    onlyOptions.options.push_back({1, 42, "Face", 0, 0});
    // data.choices still empty -- namedChoicesForModel's own guard requires both.
    result = assemble(onlyOptions, 42);
    CHECK(result.options.empty());
}

TEST_CASE("canon::assembleDefinition: an option with zero real choices is dropped entirely, "
          "same as namedChoicesForModel") {
    chrcustomization::Data data;
    data.options.push_back({1, 42, "Face", 0, 0});   // no choices anywhere in data.choices
    data.options.push_back({2, 42, "Ears", 1, 0});
    data.choices.push_back({200, 2, "Long Fin", 0});  // only option 2 has a real choice

    canon::Definition result = assemble(data, 42);
    REQUIRE(result.options.size() == 1);
    CHECK(std::get<canon::Db2Row>(result.options[0].ref.id).row == 2);
}

TEST_CASE("canon::assembleDefinition: options sorted by real orderIndex (ties by id), independent "
          "of Data::options' own storage order") {
    chrcustomization::Data data;
    // Deliberately stored out of real display order.
    data.options.push_back({10, 42, "Third", 5, 0});
    data.options.push_back({11, 42, "First", 0, 0});
    data.options.push_back({12, 42, "SecondTieA", 2, 0});
    data.options.push_back({13, 42, "SecondTieB", 2, 0});  // same orderIndex as 12, higher id
    for (uint32_t optId : {10u, 11u, 12u, 13u}) data.choices.push_back({optId * 100, optId, "Choice", 0});

    canon::Definition result = assemble(data, 42);
    REQUIRE(result.options.size() == 4);
    CHECK(result.options[0].ref.name == "First");
    CHECK(result.options[1].ref.name == "SecondTieA");  // orderIndex tie: lower id (12) first
    CHECK(result.options[2].ref.name == "SecondTieB");
    CHECK(result.options[3].ref.name == "Third");
}

TEST_CASE("canon::assembleDefinition: choices within one option sorted by real orderIndex (ties "
          "by id), independent of Data::choices' own storage order") {
    chrcustomization::Data data;
    data.options.push_back({1, 42, "Hair Color", 0, 0});
    // Stored out of order, plus a tie.
    data.choices.push_back({103, 1, "Third", 5});
    data.choices.push_back({101, 1, "First", 0});
    data.choices.push_back({104, 1, "SecondTieHighId", 2});
    data.choices.push_back({102, 1, "SecondTieLowId", 2});

    canon::Definition result = assemble(data, 42);
    REQUIRE(result.options.size() == 1);
    REQUIRE(result.options[0].choices.size() == 4);
    CHECK(result.options[0].choices[0].ref.name == "First");
    CHECK(result.options[0].choices[1].ref.name == "SecondTieLowId");  // tie: lower id (102) first
    CHECK(result.options[0].choices[2].ref.name == "SecondTieHighId");
    CHECK(result.options[0].choices[3].ref.name == "Third");
}

TEST_CASE("canon::assembleDefinition: a swatch-only choice (real Name_lang == \"0\", already "
          "empty by the time it reaches Choice::name) carries no fabricated name") {
    chrcustomization::Data data;
    data.options.push_back({1, 42, "Skin Color", 0, 0});
    data.choices.push_back({100, 1, "", 0});  // Choice::name empty -- the real swatch-only shape

    canon::Definition result = assemble(data, 42);
    REQUIRE(result.options.size() == 1);
    REQUIRE(result.options[0].choices.size() == 1);
    CHECK(result.options[0].choices[0].ref.name.empty());
    CHECK(result.options[0].choices[0].ref.source == canon::NameSource::None);
    REQUIRE(std::holds_alternative<canon::Db2Row>(result.options[0].choices[0].ref.id));
    CHECK(std::get<canon::Db2Row>(result.options[0].choices[0].ref.id).row == 100);
}

TEST_CASE("canon::assembleDefinition: an option whose ChrCustomizationCategoryID is 0 (no real "
          "category at all) carries no fabricated category grouping") {
    chrcustomization::Data data;
    data.options.push_back({1, 42, "Ears", 0, /*categoryId=*/0});
    data.choices.push_back({100, 1, "Long Fin", 0});
    // Some real category rows loaded elsewhere in Data -- must not be
    // guessed at just because they exist.
    data.categories.push_back({2, "Face", 0});

    canon::Definition result = assemble(data, 42);
    REQUIRE(result.options.size() == 1);
    CHECK(result.options[0].category.ref.name.empty());
    CHECK(result.options[0].category.ref.source == canon::NameSource::None);
}

TEST_CASE("canon::assembleDefinition: an option whose ChrCustomizationCategoryID is a dangling "
          "reference (matches no loaded Category row) carries no fabricated category grouping "
          "either -- same as namedChoicesForModel's own dangling-reference handling") {
    chrcustomization::Data data;
    data.options.push_back({1, 42, "Ears", 0, /*categoryId=*/99});  // no Category row with id 99
    data.choices.push_back({100, 1, "Long Fin", 0});

    canon::Definition result = assemble(data, 42);
    REQUIRE(result.options.size() == 1);
    CHECK(result.options[0].category.ref.name.empty());
    CHECK(result.options[0].category.ref.source == canon::NameSource::None);
}

TEST_CASE("canon::assembleDefinition: a choice with a real geoset element decodes to the same "
          "canon::Geoset::fromRawId group/variant a resolved GeosetType*100+GeosetID would") {
    chrcustomization::Data data;
    data.options.push_back({1, 42, "Ears", 0, 0});
    data.choices.push_back({100, 1, "Long Fin", 0});
    data.elements.push_back({100, /*geosetId=*/6, 0, 0, 0});
    data.geosets.push_back({6, /*geosetType=*/3, /*geosetId=*/1});  // raw id 301

    canon::Definition result = assemble(data, 42);
    REQUIRE(result.options.size() == 1);
    REQUIRE(result.options[0].choices.size() == 1);
    REQUIRE(result.options[0].choices[0].geoset.has_value());
    CHECK(result.options[0].choices[0].geoset->group == 3);
    CHECK(result.options[0].choices[0].geoset->variant == 1);
}

TEST_CASE("canon::assembleDefinition: a choice with no geoset element carries no geoset at all, "
          "not a fabricated zero one") {
    chrcustomization::Data data;
    data.options.push_back({1, 42, "Skin Color", 0, 0});
    data.choices.push_back({100, 1, "Pale", 0});  // no matching Element row anywhere

    canon::Definition result = assemble(data, 42);
    REQUIRE(result.options.size() == 1);
    REQUIRE(result.options[0].choices.size() == 1);
    CHECK_FALSE(result.options[0].choices[0].geoset.has_value());
}

TEST_CASE("canon::assembleDefinition: only options scoped to the requested chrModelId are "
          "included, same as namedChoicesForModel") {
    chrcustomization::Data data;
    data.options.push_back({1, 42, "Face (model 42)", 0, 0});
    data.options.push_back({2, 99, "Face (model 99)", 0, 0});
    data.choices.push_back({100, 1, "A", 0});
    data.choices.push_back({200, 2, "B", 0});

    canon::Definition result = assemble(data, 42);
    REQUIRE(result.options.size() == 1);
    CHECK(result.options[0].ref.name == "Face (model 42)");
}
