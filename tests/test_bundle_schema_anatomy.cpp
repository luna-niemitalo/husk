// bundle_schema on the anatomy family (REFACTOR/ANATOMY_EXCHANGE.md): a
// valid anatomy, run and result pass together, and each check beyond the
// schema fails with the expected and actual value.

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "bundle_schema.hpp"
#include "test_temp.hpp"

using namespace husk;
namespace fs = std::filesystem;

namespace {

std::string header(const std::string& kind) {
    return R"("$schema": "https://husk.invalid/schema/)" + kind + R"(.schema.json", "format": "husk-bundle", "kind": ")" + kind +
           R"(", "schema_version": "0.1.0",
  "subject": {"id": {"kind": "none"}, "name": "tail_root_test", "name_source": "authored"},
  "space": {"length_unit": "metre", "angle_unit": "radian", "time_unit": "second", "up_axis": "+z", "handedness": "right"},
  "provenance": {"input_format": "test", "sources": []},)";
}

std::string slice(const char* file, size_t offset, const char* type, int components, size_t count) {
    size_t size = std::string(type) == "u8" ? 1 : 4;
    return R"({"file": ")" + std::string(file) + R"(", "byte_offset": )" + std::to_string(offset) + R"(, "byte_length": )" +
           std::to_string(count * components * size) + R"(, "component_type": ")" + type + R"(", "component_count": )" +
           std::to_string(components) + R"(, "count": )" + std::to_string(count) + "}";
}

// anatomy.bin: a cube of skin (8 vertices, 6 quads, 12 edges) over a
// tetrahedron of muscle (4 vertices, 4 triangles), one attachment vertex.
constexpr size_t kSkinSizes = 96;     // u8 x 6
constexpr size_t kMuscleSizes = 344;  // u8 x 4
constexpr size_t kAnatomyBytes = 448;

std::string anatomyBin() {
    std::string bytes(kAnatomyBytes, '\0');
    std::fill_n(bytes.begin() + kSkinSizes, 6, '\4');
    std::fill_n(bytes.begin() + kMuscleSizes, 4, '\3');
    return bytes;
}

std::string skinSurface() {
    return R"({"positions": )" + slice("anatomy.bin", 0, "f32", 3, 8) + R"(, "face_sizes": )" + slice("anatomy.bin", kSkinSizes, "u8", 1, 6) +
           R"(, "face_vertices": )" + slice("anatomy.bin", 104, "u32", 1, 24) + R"(, "edges": )" + slice("anatomy.bin", 200, "u32", 2, 12) + "}";
}

std::string muscleSurface() {
    return R"({"positions": )" + slice("anatomy.bin", 296, "f32", 3, 4) + R"(, "face_sizes": )" + slice("anatomy.bin", kMuscleSizes, "u8", 1, 4) +
           R"(, "face_vertices": )" + slice("anatomy.bin", 348, "u32", 1, 12) + "}";
}

std::string anatomyDocument() {
    return "{" + header("anatomy") + R"(
  "definition": {
    "base_model": {"model": {"uri": "base.xml", "media_type": "model/vnd.mujoco.mjcf+xml"},
                   "name": "MS-Human-700", "revision": "test", "licence": "Apache-2.0"},
    "tissues": [
      {"id": "skin", "layer": "skin", "surface": )" + skinSurface() + R"(},
      {"id": "glmax", "layer": "muscle", "base_muscle": "glmax1_r", "surface": )" + muscleSurface() +
           R"(, "fibre_directions": )" + slice("anatomy.bin", 396, "f32", 3, 4) + R"(}],
    "attachments": [{"id": "glmax_origin", "tissue": "glmax", "body": "pelvis", "vertices": )" + slice("anatomy.bin", 444, "u32", 1, 1) + R"(}],
    "rods": [{"id": "sacrotuberous", "role": "ligament",
              "ends": [{"body": "sacrum", "point": [0, 0, 0.01]}, {"tissue": "glmax", "vertex": 2}], "via": []}],
    "parameters": [
      {"id": "laxity", "quantity": "ligament_laxity", "bodies": [], "tissues": [], "rods": ["sacrotuberous"], "rest_value": 1},
      {"id": "glute_fat", "quantity": "fat_thickness", "bodies": [], "tissues": ["glmax"], "rods": [], "rest_value": 0.01}]},
  "selection": {},
  "references": []
})";
}

std::string anatomyReference() {
    return R"({"id": "anatomy", "kind": "anatomy", "asset": {"id": {"kind": "none"}, "name": "tail_root_test", "name_source": "authored", "uri": "anatomy.json"}})";
}

std::string runDocument() {
    return "{" + header("anatomy_run") + R"(
  "definition": {"coordinates": [{"id": "hip_flexion_r", "unit": "radian"}, {"id": "tail_slide", "unit": "metre"}]},
  "selection": {"body": [{"parameter": "laxity", "value": 1.5}],
                "poses": [{"id": "rest", "values": [0, 0]}, {"id": "flex", "values": [1.2, 0.01]}]},
  "references": [)" + anatomyReference() + R"(]
})";
}

// result.bin: one pose's skin positions, edge and diagonal stretch, fit error.
std::string resultDocument() {
    return "{" + header("anatomy_result") + R"(
  "definition": {"coordinates": [{"id": "hip_flexion_r", "unit": "radian"}, {"id": "tail_slide", "unit": "metre"}],
                 "skin": {"tissue": "skin", "vertex_count": 8, "edge_count": 12, "diagonal_count": 12}},
  "selection": {"body": [{"parameter": "laxity", "value": 1.5}]},
  "references": [)" + anatomyReference() + R"(,
                 {"id": "anatomy_run", "kind": "anatomy_run", "asset": {"id": {"kind": "none"}, "name": "tail_root_test", "name_source": "authored", "uri": "run.json"}}],
  "solver": {"name": "anatomy_sim", "version": "test", "method": "vbd", "settings": [{"name": "tolerance", "value": "0.1 mm"}],
             "fit_reference": "newton_reference at 0.01 mm"},
  "poses": [{"id": "flex", "origin": "requested", "values": [1.2, 0.01], "converged": true, "iterations": 40,
             "skin_positions": )" + slice("result.bin", 0, "f32", 3, 8) + R"(, "edge_stretch": )" + slice("result.bin", 96, "f32", 1, 12) +
           R"(, "diagonal_stretch": )" + slice("result.bin", 144, "f32", 1, 12) + R"(, "fit_error": )" + slice("result.bin", 192, "f32", 1, 8) + R"(}]
})";
}

std::string replaced(std::string text, const std::string& from, const std::string& to) {
    size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    return text.replace(at, from.size(), to);
}

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

// Writes the three documents and their payloads; returns the result's path.
fs::path writeSet(const std::string& name, const std::string& anatomy, const std::string& run, const std::string& result,
                  const std::string& anatomyBytes = anatomyBin()) {
    fs::path dir = husk::test::testRunDir() / name;
    fs::remove_all(dir);
    writeText(dir / "anatomy.json", anatomy);
    writeText(dir / "run.json", run);
    writeText(dir / "result.json", result);
    writeText(dir / "base.xml", "<mujoco/>");
    writeText(dir / "anatomy.bin", anatomyBytes);
    writeText(dir / "result.bin", std::string(224, '\0'));
    return dir / "result.json";
}

std::string describe(const std::vector<bundle_schema::Problem>& problems) {
    std::string out;
    for (const bundle_schema::Problem& p : problems) out += p.where + ": " + p.message + "\n";
    return out;
}

bool mentions(const std::vector<bundle_schema::Problem>& problems, const std::string& text) {
    return std::any_of(problems.begin(), problems.end(), [&](const bundle_schema::Problem& p) {
        return p.message.find(text) != std::string::npos || p.where.find(text) != std::string::npos;
    });
}

std::vector<bundle_schema::Problem> problemsOf(const std::string& name, const std::string& anatomy, const std::string& run,
                                               const std::string& result, const std::string& anatomyBytes = anatomyBin()) {
    std::vector<bundle_schema::Problem> problems = bundle_schema::validateDocument(writeSet(name, anatomy, run, result, anatomyBytes));
    INFO(describe(problems));
    return problems;
}

}  // namespace

TEST_CASE("bundle_schema anatomy: a result validates with the run and anatomy it references") {
    std::vector<fs::path> checked;
    std::vector<bundle_schema::Problem> problems =
        bundle_schema::validateDocument(writeSet("husk-anatomy-valid", anatomyDocument(), runDocument(), resultDocument()), &checked);
    INFO(describe(problems));
    CHECK(problems.empty());
    CHECK(checked.size() == 3);
}

TEST_CASE("bundle_schema anatomy: the skin is one, quad-or-larger, with edges; face sizes sum to the corner list") {
    std::string bytes = anatomyBin();
    bytes[kSkinSizes + 2] = '\3';
    auto problems = problemsOf("husk-anatomy-skin-triangle", anatomyDocument(), runDocument(), resultDocument(), bytes);
    CHECK(mentions(problems, "expected every face to have at least 4 corners, got 1 that don't (first: face 2, 3)"));
    CHECK(mentions(problems, "expected 23 elements (one per face corner, the face sizes' sum), got 24"));
    // The result's diagonal count is the anatomy's: a triangle has none.
    CHECK(mentions(problems, "expected the anatomy's skin diagonal count 10, got 12"));

    std::string twoSkins = replaced(anatomyDocument(), R"("layer": "muscle", "base_muscle": "glmax1_r",)", R"("layer": "skin", "base_muscle": "glmax1_r",)");
    problems = problemsOf("husk-anatomy-skins", twoSkins, runDocument(), resultDocument());
    CHECK(mentions(problems, "expected exactly one skin tissue, got 2"));
    CHECK(mentions(problems, "expected 'fibre_directions' only on a muscle, got it on skin"));
    CHECK(mentions(problems, "expected edges on the skin, got none"));
    CHECK(mentions(problems, "expected every face to have at least 4 corners, got 4 that don't (first: face 0, 3)"));
}

TEST_CASE("bundle_schema anatomy: fibres on every muscle and nowhere else, a rod end of one form, unique tissue ids") {
    std::string noFibres = replaced(anatomyDocument(), R"(, "fibre_directions": )" + slice("anatomy.bin", 396, "f32", 3, 4), "");
    CHECK(mentions(problemsOf("husk-anatomy-fibres", noFibres, runDocument(), resultDocument()), "expected fibre_directions on a muscle, got none"));

    std::string shortFibres = replaced(anatomyDocument(), slice("anatomy.bin", 396, "f32", 3, 4), slice("anatomy.bin", 396, "f32", 3, 3));
    CHECK(mentions(problemsOf("husk-anatomy-fibre-count", shortFibres, runDocument(), resultDocument()),
                   "expected 4 elements (one per surface vertex), got 3"));

    std::string bothEnds = replaced(anatomyDocument(), R"({"tissue": "glmax", "vertex": 2})", R"({"tissue": "glmax", "vertex": 2, "body": "femur_r"})");
    CHECK(mentions(problemsOf("husk-anatomy-rod-end", bothEnds, runDocument(), resultDocument()), "anatomy.json#/definition/rods/0/ends/1"));

    std::string duplicateTissue = replaced(anatomyDocument(), R"("id": "glmax", "layer")", R"("id": "skin", "layer")");
    CHECK(mentions(problemsOf("husk-anatomy-duplicate", duplicateTissue, runDocument(), resultDocument()), "expected a unique id, 'skin' repeats"));
}

TEST_CASE("bundle_schema anatomy: a run's poses have one value per coordinate, unique ids, and body values of the anatomy's parameters") {
    std::string run = replaced(runDocument(), R"({"id": "flex", "values": [1.2, 0.01]})", R"({"id": "rest", "values": [1.2]})");
    run = replaced(run, R"({"parameter": "laxity", "value": 1.5})", R"({"parameter": "laxity", "value": 1.5}, {"parameter": "thigh_fat", "value": 0.02})");
    auto problems = problemsOf("husk-anatomy-run", anatomyDocument(), run, resultDocument());
    CHECK(mentions(problems, "expected 2 values (one per coordinate), got 1"));
    CHECK(mentions(problems, "expected a unique id, 'rest' repeats"));
    CHECK(mentions(problems, "expected a parameter of the referenced anatomy, got 'thigh_fat'"));
}

TEST_CASE("bundle_schema anatomy: a result's skin counts and stretch slices match the anatomy's skin, its requested poses are the run's") {
    std::string result = replaced(resultDocument(), R"("edge_count": 12)", R"("edge_count": 13)");
    result = replaced(result, slice("result.bin", 144, "f32", 1, 12), slice("result.bin", 144, "f32", 2, 6));
    result = replaced(result, R"({"id": "flex", "origin": "requested")", R"({"id": "extend", "origin": "requested")");
    auto problems = problemsOf("husk-anatomy-result", anatomyDocument(), runDocument(), result);
    CHECK(mentions(problems, "expected the anatomy's skin edge count 12, got 13"));
    CHECK(mentions(problems, "expected 13 elements (one per skin edge), got 12"));
    CHECK(mentions(problems, "expected f32 x 1, got f32 x 2"));
    CHECK(mentions(problems, "expected 12 elements (one per skin face diagonal), got 6"));
    CHECK(mentions(problems, "expected a requested pose to be one of the run's poses, got 'extend'"));

    std::string noReference = replaced(resultDocument(), R"(,
             "fit_reference": "newton_reference at 0.01 mm")", "");
    CHECK(mentions(problemsOf("husk-anatomy-fit", anatomyDocument(), runDocument(), noReference), "expected fit_reference"));

    std::string badId = replaced(resultDocument(), R"({"id": "anatomy_run", "kind": "anatomy_run")", R"({"id": "run", "kind": "anatomy_run")");
    CHECK(mentions(problemsOf("husk-anatomy-ref-id", anatomyDocument(), runDocument(), badId),
                   "expected the reference's kind 'anatomy_run' as its id, got \"run\""));
}
