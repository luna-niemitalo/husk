// bundle_schema (bundle_schema.hpp): schema/ as the format's definition --
// validation of documents against it, and the lint that keeps the format
// itself free of known antipatterns.

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "test_temp.hpp"
#include "bundle_schema.hpp"
#include "bundle_schema_embedded.hpp"
#include "writers/bundle_common.hpp"

using namespace husk;
namespace fs = std::filesystem;

namespace {

constexpr const char* kHeader = R"("format": "husk-bundle", "schema_version": "0.4.0",
  "space": {"length_unit": "yard", "angle_unit": "radian", "time_unit": "second", "up_axis": "+z", "handedness": "right"},
  "provenance": {"producer": "husk test", "input_format": "test",
                 "sources": [{"name": "listfile", "role": "names", "available": true}]},)";

std::string header(const std::string& kind) {
    return std::string(kHeader) + "\n  \"kind\": \"" + kind + "\", \"$schema\": \"https://husk.invalid/schema/" + kind + ".schema.json\",";
}

std::string slice(size_t offset, size_t length, const char* type, int components, size_t count) {
    return R"({"file": "mesh.bin", "byte_offset": )" + std::to_string(offset) + R"(, "byte_length": )" +
           std::to_string(length) + R"(, "component_type": ")" + type + R"(", "component_count": )" +
           std::to_string(components) + R"(, "count": )" + std::to_string(count) + "}";
}

// One triangle, one material, one owned placement set placing one chair.
std::string modelDocument() {
    std::string level0 = R"({"positions": )" + slice(0, 36, "f32", 3, 3) + R"(, "normals": )" + slice(36, 36, "f32", 3, 3) +
                         R"(, "uv_sets": [)" + slice(72, 24, "f32", 2, 3) + R"(], "indices": )" + slice(96, 12, "u32", 1, 3) +
                         R"(, "primitives": [{"geoset_id": 0, "geoset_group": 0, "geoset_variant": 0, "index_start": 0, "index_count": 3, "material": "00000000-0000-3000-8000-00000000000c"}],
               "attributes": []})";
    return std::string("{") + header("model") + R"(
  "subject": {"id": {"kind": "file_data_id", "value": 1}, "name": "crate", "name_source": "listfile"},
  "definition": {"placement_sets": [{"reference": "00000000-0000-3000-8000-00000000000a", "always_on": true, "instance_count": 1}]},
  "selection": {"placement_sets": []},
  "references": [{"id": "00000000-0000-3000-8000-00000000000a", "kind": "placement_set",
                  "asset": {"id": {"kind": "record_index", "value": 0}, "name": "Set_A", "name_source": "synthesized", "uri": "sets/0.json"}}],
  "resources": {
    "geometry": {
      "mesh": {"positions": )" + slice(0, 36, "f32", 3, 3) + R"(, "normals": )" + slice(36, 36, "f32", 3, 3) +
           R"(, "uv_sets": [)" + slice(72, 24, "f32", 2, 3) + R"(], "indices": )" + slice(96, 12, "u32", 1, 3) + R"(,
               "primitives": [{"geoset_id": 0, "geoset_group": 0, "geoset_variant": 0, "index_start": 0, "index_count": 3, "material": "00000000-0000-3000-8000-00000000000c"}],
               "parts": [], "portals": [], "attributes": []},
      "lod": {"max_level": 0, "levels": [)" + level0 + R"(]},
      "liquids": []},
    "rig": {"skeleton": {"joint_count": 0, "parents": )" + slice(108, 0, "i32", 1, 0) + R"(, "bind_translation": )" +
           slice(108, 0, "f32", 3, 0) + R"(, "joints": []}, "animation": []},
    "materials": [{"id": "00000000-0000-3000-8000-00000000000c", "layers": [], "unlit": false, "unfogged": false, "two_sided": false, "source": {"flags": "0x0"}}],
    "scene": {"attachments": [], "events": [], "animated_lights": [], "static_lights": []}}
})";
}

std::string setDocument() {
    return std::string("{") + header("placement_set") + R"(
  "subject": {"id": {"kind": "record_index", "value": 0}, "name": "Set_A", "name_source": "synthesized"},
  "definition": {}, "selection": {},
  "references": [{"id": "00000000-0000-3000-8000-00000000000b", "kind": "model", "asset": {"id": {"kind": "file_data_id", "value": 2}, "name": "chair", "name_source": "listfile"}}],
  "instances": [{"id": 7, "reference": "00000000-0000-3000-8000-00000000000b", "translation": [1, 2, 3], "rotation": [0, 0, 0, 1], "scale": 1,
                 "selection": {"placement_sets": []}, "source": {"flags": "0x0"}}]
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

fs::path writeBundle(const std::string& name, const std::string& model, const std::string& set) {
    fs::path dir = husk::test::testRunDir() / name;
    fs::remove_all(dir);
    writeText(dir / "manifest.json", model);
    writeText(dir / "sets" / "0.json", set);
    writeText(dir / "mesh.bin", std::string(108, '\0'));
    return dir;
}

bool mentions(const std::vector<bundle_schema::Problem>& problems, const std::string& text) {
    return std::any_of(problems.begin(), problems.end(), [&](const bundle_schema::Problem& p) {
        return p.message.find(text) != std::string::npos || p.where.find(text) != std::string::npos;
    });
}

std::string describe(const std::vector<bundle_schema::Problem>& problems) {
    std::string out;
    for (const bundle_schema::Problem& p : problems) out += p.where + ": " + p.message + "\n";
    return out;
}

}  // namespace

TEST_CASE("bundle_schema: the embedded schema defines animation_fallbacks, appearances, creatures, effect, material, model, placement_set, technique and world, and passes its own lint") {
    CHECK(bundle_schema::kinds() == std::vector<std::string>{"animation_fallbacks", "appearances", "creatures", "effect",
                                                             "material", "model", "placement_set", "technique", "world"});
    std::vector<bundle_schema::Problem> problems = bundle_schema::lintSchemas();
    INFO(describe(problems));
    CHECK(problems.empty());
}

namespace {

// A placement set placing one instance of `effect`, written inline.
std::string effectSetDocument(const std::string& effect, const std::string& instance) {
    return std::string("{") + header("placement_set") + R"(
  "subject": {"id": {"kind": "record_index", "value": 0}, "name": "Set_A", "name_source": "synthesized"},
  "definition": {}, "selection": {},
  "references": [{"id": "00000000-0000-3000-8000-00000000000c", "kind": "effect", "asset": {"id": {"kind": "none"}, "name": "glow", "name_source": "synthesized"},
                  "document": )" + effect + R"(}],
  "instances": [)" + instance + R"(]
})";
}

std::string glowEffect(const std::string& status = R"("status": "placeholder",)") {
    return std::string("{") + header("effect") + R"(
  "subject": {"id": {"kind": "none"}, "name": "glow", "name_source": "synthesized"},
  "definition": {)" + status + R"( "inputs": {
    "center": {"kind": "position", "description": "Centre."},
    "radius": {"kind": "number", "unit": "yard", "description": "Radius."}}},
  "selection": {}, "references": [],
  "sources": [{"id": "glow", "region": {"kind": "sphere", "center": {"input": "center"},
               "inner_radius": 0, "outer_radius": {"input": "radius"}}}]
})";
}

constexpr const char* kGlowInstance = R"({"id": 1, "reference": "00000000-0000-3000-8000-00000000000c", "translation": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": 1,
  "inputs": {"center": [1, 2, 3], "radius": 4},
  "style": {"glow": {"distance_fog": {"linear": {"start": 1, "end": 10, "color": [1, 1, 1]}}}},
  "source": {"flags": "0x0", "record": {"table": "test", "index": 0}}})";

fs::path writeSet(const std::string& name, const std::string& set) {
    fs::path file = husk::test::testRunDir() / name / "set.json";
    fs::remove_all(file.parent_path());
    writeText(file, set);
    return file;
}

}  // namespace

TEST_CASE("bundle_schema: every effect husk writes validates") {
    for (const char* name : {"classic_local_fog", "classic_fog_exit_blend", "classic_ambient_global",
                             "classic_ambient_point", "classic_ambient_box", "classic_particulate_volume",
                             "classic_volumetric_fog", "classic_map_particulate_volume", "classic_m2_particle_emitter",
                             "classic_m2_model_particle_emitter", "classic_m2_ribbon", "classic_zone_light_global",
                             "classic_zone_light"}) {
        CAPTURE(name);
        fs::path file = husk::test::testRunDir() / "husk-schema-effects" / (std::string(name) + ".json");
        fs::create_directories(file.parent_path());
        writers::writeEffectDocument(name, file, writers::Provenance{"husk test", "test", {}});
        std::vector<bundle_schema::Problem> problems = bundle_schema::validateDocument(file);
        INFO(describe(problems));
        CHECK(problems.empty());
    }
}

TEST_CASE("bundle_schema: every technique husk carries validates, with its references") {
    fs::path dir = husk::test::testRunDir() / "husk-schema-techniques";
    fs::remove_all(dir);
    std::vector<std::string> names;
    for (const auto& t : bundle_schema::embeddedTechniques()) names.emplace_back(t.name);
    CHECK(names.size() == 72);
    writers::writeTechniqueDocuments(names, dir, writers::Provenance{"husk test", "test", {}});
    for (const std::string& name : names) {
        CAPTURE(name);
        std::vector<bundle_schema::Problem> problems = bundle_schema::validateDocument(dir / writers::techniqueUri(name));
        INFO(describe(problems));
        CHECK(problems.empty());
    }
}

TEST_CASE("bundle_schema: a technique's expressions are one node kind each and name declared inputs") {
    fs::path file = husk::test::testRunDir() / "husk-schema-technique-bad" / "t.json";
    fs::remove_all(file.parent_path());
    writeText(file, std::string("{") + header("technique") + R"( "subject": {"id": {"kind": "none"}, "name": "t", "name_source": "synthesized"},
        "definition": {"family": "m2_pixel_shader", "status": "verified", "evidence": "test",
                       "inputs": {"t0": {"kind": "texture_unit", "unit": 0, "description": "x"}},
                       "outputs": {"diffuse": {"op": "lerp", "args": [{"input": "t0"}, {"input": "t9"}]},
                                   "alpha": {"input": "t0", "const": 1}}},
        "selection": {}, "references": []})");
    std::vector<bundle_schema::Problem> problems = bundle_schema::validateDocument(file);
    CHECK(mentions(problems, "expected 3 operands for lerp, got 2"));
    CHECK(mentions(problems, "expected an input definition.inputs declares, got 't9'"));
    CHECK(mentions(problems, "expected exactly one of input, term, const, op, got 2"));
}

TEST_CASE("bundle_schema: a technique's input coordinates are checked, and a cycle through one fails") {
    fs::path file = husk::test::testRunDir() / "husk-schema-technique-cycle" / "t.json";
    fs::remove_all(file.parent_path());
    writeText(file, std::string("{") + header("technique") + R"( "subject": {"id": {"kind": "none"}, "name": "t", "name_source": "synthesized"},
        "definition": {"family": "wmo_shader", "status": "verified", "evidence": "test",
                       "inputs": {"t0": {"kind": "texture_unit", "unit": 0, "description": "x",
                                         "coordinate": {"op": "div", "args": [{"term": "offset"}, {"input": "uv9"}]}},
                                  "t1": {"kind": "texture_unit", "unit": 1, "description": "x",
                                         "coordinate": {"op": "max", "args": [{"input": "t0", "channels": "rg"}, {"const": 0}]}}},
                       "terms": {"offset": {"input": "t1", "channels": "rg"}},
                       "outputs": {"diffuse": {"input": "t0", "channels": "rgb"}}},
        "selection": {}, "references": []})");
    std::vector<bundle_schema::Problem> problems = bundle_schema::validateDocument(file);
    CHECK(mentions(problems, "expected an input definition.inputs declares, got 'uv9'"));
    CHECK(mentions(problems, "expected terms and input coordinates without a cycle"));
}

TEST_CASE("bundle_schema: min, dot and tangent_frame are ops with their operand counts") {
    auto technique = [](const char* outputs) {
        return std::string("{") + header("technique") + R"( "subject": {"id": {"kind": "none"}, "name": "t", "name_source": "synthesized"},
        "definition": {"family": "wmo_shader", "status": "verified", "evidence": "test",
                       "inputs": {"t0": {"kind": "texture_unit", "unit": 0, "description": "x"},
                                  "p": {"kind": "renderer_value", "description": "x"},
                                  "n": {"kind": "renderer_value", "description": "x"},
                                  "uv0": {"kind": "vertex_value", "description": "x"}},
                       "outputs": )" + outputs + R"(},
        "selection": {}, "references": []})";
    };
    fs::path file = husk::test::testRunDir() / "husk-schema-technique-ops" / "t.json";
    fs::remove_all(file.parent_path());

    writeText(file, technique(R"({"diffuse": {"op": "min", "args": [{"input": "t0", "channels": "rgb"}, {"const": 0.5}, {"input": "n"}]},
        "alpha": {"op": "dot", "args": [{"input": "n"}, {"input": "p"}]},
        "emissive": {"op": "tangent_frame", "args": [{"input": "p"}, {"input": "p"}, {"input": "n"}, {"input": "uv0"}]}})"));
    std::vector<bundle_schema::Problem> problems = bundle_schema::validateDocument(file);
    INFO(describe(problems));
    CHECK(problems.empty());

    writeText(file, technique(R"({"diffuse": {"op": "min", "args": [{"input": "t0"}]},
        "alpha": {"op": "dot", "args": [{"input": "n"}, {"input": "p"}, {"input": "p"}]},
        "emissive": {"op": "tangent_frame", "args": [{"input": "p"}, {"input": "n"}, {"input": "uv0"}]}})"));
    problems = bundle_schema::validateDocument(file);
    CHECK(mentions(problems, "expected 2+ operands for min, got 1"));
    CHECK(mentions(problems, "expected 2 operands for dot, got 3"));
    CHECK(mentions(problems, "expected 4 operands for tangent_frame, got 3"));
}

TEST_CASE("bundle_schema: an inline effect's instance is checked against the inputs it declares") {
    std::vector<bundle_schema::Problem> warnings;
    std::vector<bundle_schema::Problem> problems =
        bundle_schema::validateDocument(writeSet("husk-schema-effect-ok", effectSetDocument(glowEffect(), kGlowInstance)), nullptr, &warnings);
    INFO(describe(problems));
    CHECK(problems.empty());
    CHECK(mentions(warnings, "instance of placeholder effect 'glow'"));

    warnings.clear();
    problems = bundle_schema::validateDocument(writeSet("husk-schema-effect-final", effectSetDocument(glowEffect(""), kGlowInstance)), nullptr, &warnings);
    CHECK(problems.empty());
    CHECK(warnings.empty());

    std::string set = effectSetDocument(glowEffect(), replaced(kGlowInstance, R"("radius": 4)", R"("radius": [4, 4, 4])"));
    CHECK(mentions(bundle_schema::validateDocument(writeSet("husk-schema-effect-kind", set)),
                   "input 'radius' is a number: expected a number, got [4,4,4]"));

    set = effectSetDocument(glowEffect(), replaced(kGlowInstance, R"("radius": 4)", R"("radius": 4, "spline": [])"));
    CHECK(mentions(bundle_schema::validateDocument(writeSet("husk-schema-effect-extra", set)), "got 'spline'"));

    set = effectSetDocument(glowEffect(), replaced(kGlowInstance, R"(, "radius": 4)", ""));
    CHECK(mentions(bundle_schema::validateDocument(writeSet("husk-schema-effect-missing", set)),
                   "expected input 'radius' (number), which has no default, got none"));

    set = effectSetDocument(glowEffect(), replaced(kGlowInstance, R"("style": {"glow")", R"("style": {"halo")"));
    CHECK(mentions(bundle_schema::validateDocument(writeSet("husk-schema-effect-style", set)), "got 'halo'"));

    set = effectSetDocument(glowEffect(), replaced(kGlowInstance, R"("source":)",
                                                    R"("provenance": {"inputs.radius": {"kind": "derived", "rule": "r"},
                                                                      "inputs.size": {"kind": "derived", "rule": "r"}}, "source":)"));
    std::vector<bundle_schema::Problem> provenance = bundle_schema::validateDocument(writeSet("husk-schema-effect-provenance", set));
    CHECK(mentions(provenance, "got 'inputs.size'"));
    CHECK_FALSE(mentions(provenance, "got 'inputs.radius'"));
}

TEST_CASE("bundle_schema: an effect's bindings name declared inputs, and every input is bound") {
    std::string unbound = replaced(glowEffect(), R"("outer_radius": {"input": "radius"})", R"("outer_radius": 5)");
    CHECK(mentions(bundle_schema::validateDocument(writeSet("husk-schema-effect-unbound", effectSetDocument(unbound, kGlowInstance))),
                   "expected input 'radius' to be bound by a source"));

    std::string undeclared = replaced(glowEffect(), R"({"input": "radius"})", R"({"input": "size"})");
    CHECK(mentions(bundle_schema::validateDocument(writeSet("husk-schema-effect-undeclared", effectSetDocument(undeclared, kGlowInstance))),
                   "expected a binding to a declared input, got 'size'"));

    std::string jargon = replaced(replaced(glowEffect(), R"("radius": {"kind")", R"("mfog_radius": {"kind")"),
                                  R"({"input": "radius"})", R"({"input": "mfog_radius"})");
    CHECK(mentions(bundle_schema::validateDocument(writeSet("husk-schema-effect-jargon", effectSetDocument(jargon, kGlowInstance))),
                   "contains source-format name 'mfog'"));
}

TEST_CASE("bundle_schema: a well-formed model validates, together with the placement set it references") {
    fs::path dir = writeBundle("husk-schema-valid", modelDocument(), setDocument());
    std::vector<fs::path> checked;
    std::vector<bundle_schema::Problem> problems = bundle_schema::validateDocument(dir, &checked);
    INFO(describe(problems));
    CHECK(problems.empty());
    CHECK(checked.size() == 2);
}

TEST_CASE("bundle_schema: schema violations -- an unknown key, a missing kind, a wrong constant") {
    std::string model = modelDocument();
    fs::path dir = writeBundle("husk-schema-closed", replaced(model, R"("name": "crate",)", R"("name": "crate", "texture_path": "x",)"), setDocument());
    CHECK_FALSE(bundle_schema::validateDocument(dir).empty());

    dir = writeBundle("husk-schema-kind", replaced(model, R"("kind": "model",)", ""), setDocument());
    CHECK(mentions(bundle_schema::validateDocument(dir), "expected one of the document kinds"));

    dir = writeBundle("husk-schema-pointer", replaced(model, "schema/model.schema.json", "schema/world.schema.json"), setDocument());
    CHECK(mentions(bundle_schema::validateDocument(dir),
                   R"(expected "https://husk.invalid/schema/model.schema.json" for kind 'model', got "https://husk.invalid/schema/world.schema.json")"));
    dir = writeBundle("husk-schema-no-pointer",
                      replaced(model, R"("$schema": "https://husk.invalid/schema/model.schema.json",)", ""), setDocument());
    CHECK(mentions(bundle_schema::validateDocument(dir), "for kind 'model', got none"));

    dir = writeBundle("husk-schema-units", replaced(model, R"("length_unit": "yard")", R"("length_unit": "metre")"), setDocument());
    CHECK_FALSE(bundle_schema::validateDocument(dir).empty());
}

TEST_CASE("bundle_schema: structural checks the schema can't express, each reporting expected and actual") {
    std::string model = modelDocument();

    fs::path dir = writeBundle("husk-schema-index", replaced(model, R"("selection": {"placement_sets": []})", R"("selection": {"placement_sets": [1]})"), setDocument());
    std::vector<bundle_schema::Problem> problems = bundle_schema::validateDocument(dir);
    CHECK(mentions(problems, "expected an index < 1 into '/definition/placement_sets', got 1"));

    dir = writeBundle("husk-schema-material-id",
                      replaced(model, R"("material": "00000000-0000-3000-8000-00000000000c")", R"("material": "00000000-0000-3000-8000-00000000000d")"),
                      setDocument());
    CHECK(mentions(bundle_schema::validateDocument(dir),
                   "expected the id of an entry of '/resources/materials', got '00000000-0000-3000-8000-00000000000d'"));

    dir = writeBundle("husk-schema-length", replaced(model, R"("byte_length": 12)", R"("byte_length": 16)"), setDocument());
    CHECK(mentions(bundle_schema::validateDocument(dir), "expected byte_length 12"));

    dir = writeBundle("husk-schema-bounds", model, setDocument());
    writeText(dir / "mesh.bin", std::string(100, '\0'));
    CHECK(mentions(bundle_schema::validateDocument(dir), "within 'mesh.bin' (100 bytes)"));

    dir = writeBundle("husk-schema-escape", replaced(model, R"("file": "mesh.bin")", R"("file": "../mesh.bin")"), setDocument());
    CHECK(mentions(bundle_schema::validateDocument(dir), "inside the document's directory"));

    dir = writeBundle("husk-schema-set", model,
                      replaced(setDocument(), R"("reference": "00000000-0000-3000-8000-00000000000b")",
                               R"("reference": "00000000-0000-3000-8000-0000000000ff")"));
    problems = bundle_schema::validateDocument(dir);
    CHECK(mentions(problems, "sets/0.json"));
    CHECK(mentions(problems, "expected the id of an entry of '/references', got '00000000-0000-3000-8000-0000000000ff'"));

    dir = writeBundle("husk-schema-uri", replaced(model, R"("uri": "sets/0.json")", R"("uri": "sets/9.json")"), setDocument());
    CHECK(mentions(bundle_schema::validateDocument(dir), "expected 'sets/9.json' to exist"));

    dir = writeBundle("husk-schema-max-level", replaced(model, R"("max_level": 0)", R"("max_level": 2)"), setDocument());
    CHECK(mentions(bundle_schema::validateDocument(dir), "expected the last level's index, 0, got 2"));

    // The second "index_count": 3 is level 0's copy of the mesh's primitive.
    std::string level0Differs = model;
    level0Differs.replace(level0Differs.rfind(R"("index_count": 3)"), 16, R"("index_count": 0)");
    dir = writeBundle("husk-schema-level0", level0Differs, setDocument());
    CHECK(mentions(bundle_schema::validateDocument(dir), "expected mesh level 0 to repeat resources.geometry.mesh's 'primitives'"));
}

TEST_CASE("bundle_schema: the lint rejects each known antipattern in a kind schema") {
    constexpr const char* kBad = R"({
      "$schema": "http://json-schema.org/draft-07/schema#", "x-status": "implemented",
      "type": "object", "description": "A bad kind.",
      "properties": {
        "format": {"const": "husk-bundle", "description": "d", "x-layer": "contract"},
        "dds_texture": {"type": "string", "description": "d", "x-layer": "resource"},
        "mpvd_record": {"type": "string", "description": "d", "x-layer": "resource"},
        "source_flags": {"type": "integer", "description": "d", "x-layer": "resource"},
        "undocumented": {"type": "string", "x-layer": "resource"},
        "loose": {"type": "object", "description": "d", "x-layer": "resource", "properties": {"a": {"type": "string", "description": "d"}}},
        "speed": {"type": "number", "description": "d", "x-layer": "resource"},
        "kind_of_image": {"enum": ["png", "other"], "description": "d", "x-layer": "resource"},
        "pick": {"type": "integer", "description": "d", "x-layer": "selection", "x-index-into": "/nowhere"},
        "uri": {"type": "string", "description": "d", "x-layer": "resource"},
        "CamelKey": {"type": "string", "description": "d", "x-layer": "resource"},
        "unlayered": {"type": "string", "description": "d"}
      },
      "required": ["format"], "additionalProperties": false
    })";
    std::vector<bundle_schema::Problem> problems = bundle_schema::lintSchemaText("bad", kBad);
    INFO(describe(problems));
    CHECK(mentions(problems, "missing contract header property 'subject'"));
    CHECK(mentions(problems, "'dds_texture' names a storage container"));
    CHECK(mentions(problems, "contains source-format name 'mpvd'"));
    CHECK(mentions(problems, "'source_flags' is raw source data"));
    CHECK(mentions(problems, "property 'undocumented' has no description"));
    CHECK(mentions(problems, "object is open"));
    CHECK(mentions(problems, "'speed' has no x-unit"));
    CHECK(mentions(problems, "enum value 'png'"));
    CHECK(mentions(problems, "x-index-into '/nowhere' names nothing"));
    CHECK(mentions(problems, "key 'uri' holds a path"));
    CHECK(mentions(problems, "'CamelKey' is not lowercase snake_case"));
    CHECK(mentions(problems, "'unlayered' has no x-layer"));
}

TEST_CASE("bundle_schema: the lint walks kind-local definitions and map values, and checks x-source's forms") {
    constexpr const char* kBad = R"({
      "$schema": "http://json-schema.org/draft-07/schema#", "x-status": "implemented",
      "type": "object", "description": "A bad kind.",
      "properties": {
        "names": {"type": "object", "description": "d", "x-layer": "resource",
                  "additionalProperties": {"type": "object", "properties": {"Bad": {"type": "string", "description": "d"}},
                                           "additionalProperties": false}},
        "a": {"type": "string", "description": "d", "x-layer": "resource", "x-source": [{"table": "AreaTable", "column": "Flags[0]"}, {"listfile": true}]},
        "b": {"type": "string", "description": "d", "x-layer": "resource", "x-source": [{"file": "mdx", "field": "x"}]},
        "c": {"type": "string", "description": "d", "x-layer": "resource", "x-source": [{"derived": "Not Snake"}]},
        "d": {"type": "string", "description": "d", "x-layer": "resource", "x-source": [{"format": true, "note": "x"}]},
        "e": {"type": "string", "description": "d", "x-layer": "resource", "x-source": {"format": true}},
        "f": {"type": "string", "description": "d", "x-layer": "resource", "x-source": [{"guess": true}]}
      },
      "definitions": {"local": {"type": "object", "properties": {"undocumented": {"type": "string"}}, "additionalProperties": false}},
      "additionalProperties": false
    })";
    std::vector<bundle_schema::Problem> problems = bundle_schema::lintSchemaText("bad", kBad);
    INFO(describe(problems));
    CHECK(mentions(problems, "'Bad' is not lowercase snake_case"));
    CHECK(mentions(problems, "definition 'local' has no description"));
    CHECK(mentions(problems, "property 'undocumented' has no description"));
    CHECK_FALSE(mentions(problems, "/properties/a/x-source"));
    CHECK(mentions(problems, R"(expected 'file' to be a source file kind, got "mdx")"));
    CHECK(mentions(problems, R"(expected 'derived' to be a snake_case rule name, got "Not Snake")"));
    CHECK(mentions(problems, "expected only 'format' in this origin, got 'note'"));
    CHECK(mentions(problems, "expected a non-empty array of origins"));
    CHECK(mentions(problems, R"(expected an origin object keyed table, file, derived, authored, stated_by, listfile or format, got {"guess":true})"));
}

TEST_CASE("bundle_schema: the lint rejects DB2 table and column names and as-stored values outside source") {
    constexpr const char* kBad = R"({
      "$schema": "http://json-schema.org/draft-07/schema#", "x-status": "implemented",
      "type": "object", "description": "A bad kind.",
      "properties": {
        "item_display_info": {"type": "integer", "description": "d", "x-layer": "resource", "x-source": [{"table": "ItemDisplayInfo"}]},
        "texture_type": {"type": "integer", "description": "d", "x-layer": "resource"},
        "liquid_type": {"type": "integer", "description": "d", "x-layer": "resource", "x-source": [{"table": "LiquidType"}]},
        "model_resources_id": {"type": "integer", "description": "d", "x-layer": "resource"},
        "rank": {"type": "integer", "description": "The rank as stored.", "x-layer": "resource"},
        "map": {"type": "string", "description": "d", "x-layer": "resource", "x-source": [{"table": "Map", "column": "Directory"}]},
        "source": {"type": "object", "description": "d", "x-layer": "resource", "additionalProperties": false, "properties": {
          "raw": {"type": "object", "description": "d", "additionalProperties": false, "properties": {
            "item_display_info": {"type": "integer", "description": "As stored."},
            "texture_type": {"type": "integer", "description": "As stored."}}}}}
      },
      "additionalProperties": false
    })";
    std::vector<bundle_schema::Problem> problems = bundle_schema::lintSchemaText("bad", kBad);
    INFO(describe(problems));
    CHECK(mentions(problems, "key 'item_display_info' contains the DB2 table name 'item_display_info'"));
    CHECK(mentions(problems, "key 'texture_type' is a DB2 column name"));
    CHECK(mentions(problems, "key 'model_resources_id' is a DB2 column name"));
    CHECK(mentions(problems, "key 'rank' is described as kept as stored"));
    // Two-word table names are often the plain meaning.
    CHECK_FALSE(mentions(problems, "key 'liquid_type'"));
    CHECK_FALSE(mentions(problems, "key 'map'"));
    CHECK_FALSE(mentions(problems, "/properties/source/"));
}

TEST_CASE("bundle_schema: schema-doc lists every kind's document paths with type, allowed values, unit, links and required") {
    std::string doc = bundle_schema::schemaDoc();
    CHECK(doc == bundle_schema::schemaDoc());
    for (const std::string& kind : bundle_schema::kinds()) {
        CAPTURE(kind);
        CHECK(doc.find("## " + kind + "\n\n`$schema`: `https://husk.invalid/schema/" + kind + ".schema.json`") != std::string::npos);
    }
    auto row = [&](const std::string& path) {
        size_t at = doc.find("| `" + path + "` |");
        REQUIRE(at != std::string::npos);
        return doc.substr(at, doc.find('\n', at) - at);
    };
    // The first kind's row: kinds are listed in sorted order.
    CHECK(row("$schema").find("| yes | constant | = `https://husk.invalid/schema/animation_fallbacks.schema.json` |") != std::string::npos);
    CHECK(row("sources[].region{kind=sphere}.inner_radius").find("| yes | `bound_number` (one of 2) |  | yard |") != std::string::npos);
    CHECK(row("displays[].model").find("| id in `/references` |") != std::string::npos);
    CHECK(row("definition.inputs.<name>.clock").find("one of `seconds`, `age`, `day_time`") != std::string::npos);
    CHECK(row("definition.inputs.<name>.item").find("recursive") != std::string::npos);
    CHECK(row("provenance.sources").find("and whether it could.<br>*Why:* An absent value") != std::string::npos);
    CHECK(row("provenance.sources[].name").find("e.g. a table name") != std::string::npos);
    CHECK(row("provenance.sources[].name").find("Why:") == std::string::npos);
}

TEST_CASE("bundle_schema: source gaps name a schema location and a document path, once each") {
    std::vector<bundle_schema::Problem> gaps = bundle_schema::sourceGaps();
    REQUIRE_FALSE(gaps.empty());
    std::vector<std::string> where;
    for (const bundle_schema::Problem& g : gaps) where.push_back(g.where);
    CHECK(std::is_sorted(where.begin(), where.end()));
    CHECK(std::adjacent_find(where.begin(), where.end()) == where.end());
    CHECK(gaps.front().message.find("document path ") != std::string::npos);

    // The pilot annotation: the effect schema and the contract header's
    // shared definitions resolve everywhere.
    for (const char* annotated : {"effect.schema.json#", "common.schema.json#/definitions/space/",
                                  "common.schema.json#/definitions/provenance/", "common.schema.json#/definitions/ref/",
                                  "common.schema.json#/definitions/asset_ref/", "common.schema.json#/definitions/reference/"}) {
        CAPTURE(annotated);
        CHECK_FALSE(mentions(gaps, annotated));
    }
    std::string doc = bundle_schema::schemaDoc();
    CHECK(doc.find("| `sources[].id` | yes | string |  |  |  | authored in `effects/<name>.json` (from `sources`) |") != std::string::npos);
    CHECK(doc.find("| `subject.name` |  | string | length ≥ 1 |  |  | stated per value by `name_source` |") != std::string::npos);
}
