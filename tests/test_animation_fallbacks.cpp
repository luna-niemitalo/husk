// The animation_fallbacks document (schema/animation_fallbacks.schema.json,
// REFACTOR/ANIMATION.md "Fallbacks"): a model's one reference to it, inline
// or the tree's shared file, and validate-bundle following both forms.

#include <doctest/doctest.h>

#include <sstream>

#include "test_bundle_fixtures.hpp"
#include "test_temp.hpp"
#include "writers/bundle_common.hpp"

namespace {

canon::Ref animation(uint32_t row, const std::string& name) {
    return canon::Ref{canon::Db2Row{"AnimationData", row}, name,
                      name.empty() ? canon::NameSource::None : canon::NameSource::Community};
}

canon::AnimationFallbacks table() {
    return canon::AnimationFallbacks{animation(0, "Stand"),
                                     {{animation(5, "Run"), animation(4, "Walk")}, {animation(6, "Dead"), animation(1, "")}},
                                     ""};
}

const nlohmann::json& fallbacksReference(const nlohmann::json& manifest) {
    const nlohmann::json* found = nullptr;
    for (const auto& r : manifest["references"]) {
        if (r["kind"] == "animation_fallbacks") found = &r;
    }
    REQUIRE(found);
    return *found;
}

void writeSharedTable(const fs::path& file) {
    std::ostringstream text;
    husk::json::Writer w(text);
    writers::writeAnimationFallbacksDocument(w, table(), testProvenance());
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text.str();
}

std::string describe(const std::vector<bundle_schema::Problem>& problems) {
    std::string out;
    for (const bundle_schema::Problem& p : problems) out += p.where + ": " + p.message + "\n";
    return out;
}

}  // namespace

TEST_CASE("animation fallbacks inline: the model's reference embeds the table, entries keyed by the animation's identity") {
    canon::Model model = buildSyntheticModel();
    model.animationFallbacks = table();
    fs::path dir = husk::test::testRunDir() / "husk-test-fallbacks-inline";
    fs::remove_all(dir);
    writers::writeBundle(model, dir, testProvenance());

    nlohmann::json manifest = parseManifest(dir);
    CHECK_FALSE(manifest["resources"]["rig"].contains("animation_fallbacks"));
    const nlohmann::json& reference = fallbacksReference(manifest);
    CHECK(reference["asset"]["name"] == "animation_fallbacks");
    CHECK_FALSE(reference["asset"].contains("uri"));
    const nlohmann::json& document = reference["document"];
    CHECK(document["kind"] == "animation_fallbacks");
    CHECK(document["otherwise"]["id"]["row"] == 0);
    REQUIRE(document["fallbacks"].size() == 2);
    const nlohmann::json& run = document["fallbacks"][0];
    CHECK(run["id"] == husk::nameUuid("animation_fallback/db2_row/AnimationData/5"));
    CHECK(run["animation"]["id"]["row"] == 5);
    CHECK(run["animation"]["name"] == "Run");
    CHECK(run["fallback"]["id"]["row"] == 4);
    CHECK_FALSE(document["fallbacks"][1]["fallback"].contains("name"));

    std::vector<bundle_schema::Problem> problems = bundle_schema::validateDocument(dir);
    INFO(describe(problems));
    CHECK(problems.empty());
}

TEST_CASE("animation fallbacks external: the reference names the tree's shared file by uri, with the inline form's id") {
    fs::path tree = husk::test::testRunDir() / "husk-test-fallbacks-external";
    fs::remove_all(tree);
    writeSharedTable(tree / "animation_fallbacks.json");
    canon::Model model = buildSyntheticModel();
    model.animationFallbacks = canon::AnimationFallbacks{{}, {}, "../../animation_fallbacks.json"};
    fs::path bundle = tree / "models" / "m.bundle";
    writers::writeBundle(model, bundle, testProvenance());

    nlohmann::json manifest = parseManifest(bundle);
    const nlohmann::json& reference = fallbacksReference(manifest);
    CHECK_FALSE(reference.contains("document"));
    CHECK(reference["asset"]["uri"] == "../../animation_fallbacks.json");
    canon::Ref list{canon::None{}, "animation_fallbacks", canon::NameSource::Synthesized};
    CHECK(reference["id"] == writers::referenceId("animation_fallbacks", list));

    std::vector<fs::path> checked;
    std::vector<bundle_schema::Problem> problems = bundle_schema::validateDocument(bundle, &checked);
    INFO(describe(problems));
    CHECK(problems.empty());
    CHECK(std::find(checked.begin(), checked.end(), fs::weakly_canonical(tree / "animation_fallbacks.json")) != checked.end());
}

TEST_CASE("animation fallbacks external: a missing shared file fails validation naming the path expected") {
    fs::path tree = husk::test::testRunDir() / "husk-test-fallbacks-missing";
    fs::remove_all(tree);
    canon::Model model = buildSyntheticModel();
    model.animationFallbacks = canon::AnimationFallbacks{{}, {}, "../../animation_fallbacks.json"};
    fs::path bundle = tree / "models" / "m.bundle";
    writers::writeBundle(model, bundle, testProvenance());

    std::vector<bundle_schema::Problem> problems = bundle_schema::validateDocument(bundle);
    INFO(describe(problems));
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].where.ends_with("/asset/uri"));
    CHECK(problems[0].message == "expected '../../animation_fallbacks.json' to exist relative to the document, got no file at " +
                                     (tree / "animation_fallbacks.json").lexically_normal().string());
}
