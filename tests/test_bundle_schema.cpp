// bundle_schema (bundle_schema.hpp): schema/ as the format's definition --
// validation of documents against it, and the lint that keeps the format
// itself free of known antipatterns.

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "bundle_schema.hpp"
#include "writers/bundle_common.hpp"

using namespace husk;
namespace fs = std::filesystem;

namespace {

constexpr const char* kHeader = R"("format": "husk-bundle", "schema_version": "1.0.0",
  "space": {"length_unit": "yard", "angle_unit": "radian", "time_unit": "second", "up_axis": "+z", "handedness": "right"},
  "provenance": {"producer": "husk test", "exported_at": "2026-10-10T00:00:00Z", "input_format": "test",
                 "sources": [{"name": "listfile", "role": "names", "available": true}]},)";

std::string slice(size_t offset, size_t length, const char* type, int components, size_t count) {
    return R"({"file": "mesh.bin", "byte_offset": )" + std::to_string(offset) + R"(, "byte_length": )" +
           std::to_string(length) + R"(, "component_type": ")" + type + R"(", "component_count": )" +
           std::to_string(components) + R"(, "count": )" + std::to_string(count) + "}";
}

// One triangle, one material, one owned placement set placing one chair.
std::string modelDocument() {
    return std::string("{") + kHeader + R"(
  "kind": "model",
  "subject": {"id": {"kind": "file_data_id", "value": 1}, "name": "crate", "name_source": "listfile"},
  "definition": {"placement_sets": [{"reference": 0, "always_on": true, "instance_count": 1}]},
  "selection": {"placement_sets": []},
  "references": [{"kind": "placement_set",
                  "asset": {"id": {"kind": "record_index", "value": 0}, "name": "Set_A", "name_source": "synthesized", "uri": "sets/0.json"}}],
  "resources": {
    "geometry": {
      "mesh": {"positions": )" + slice(0, 36, "f32", 3, 3) + R"(, "normals": )" + slice(36, 36, "f32", 3, 3) +
           R"(, "uv0": )" + slice(72, 24, "f32", 2, 3) + R"(, "indices": )" + slice(96, 12, "u32", 1, 3) + R"(,
               "primitives": [{"geoset_id": 0, "geoset_group": 0, "geoset_variant": 0, "index_start": 0, "index_count": 3, "material_index": 0}],
               "parts": [], "portals": [], "attributes": []},
      "liquids": []},
    "rig": {"skeleton": {"joint_count": 0, "parents": )" + slice(108, 0, "i32", 1, 0) + R"(, "bind_translation": )" +
           slice(108, 0, "f32", 3, 0) + R"(, "joints": []}, "animation": []},
    "materials": [{"layers": [], "unlit": false, "unfogged": false, "two_sided": false, "source": {"flags": "0x0"}}],
    "scene": {"attachments": [], "events": [], "animated_lights": [], "static_lights": [],
              "ambient_color_volumes": [], "ambient_particle_volumes": [], "ribbon_emitters": [], "particle_emitters": []}}
})";
}

std::string setDocument() {
    return std::string("{") + kHeader + R"(
  "kind": "placement_set",
  "subject": {"id": {"kind": "record_index", "value": 0}, "name": "Set_A", "name_source": "synthesized"},
  "definition": {}, "selection": {},
  "references": [{"kind": "model", "asset": {"id": {"kind": "file_data_id", "value": 2}, "name": "chair", "name_source": "listfile"}}],
  "instances": [{"id": 7, "reference": 0, "translation": [1, 2, 3], "rotation": [0, 0, 0, 1], "scale": 1,
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
    fs::path dir = fs::temp_directory_path() / name;
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

TEST_CASE("bundle_schema: the embedded schema defines effect, model, placement_set and world, and passes its own lint") {
    CHECK(bundle_schema::kinds() == std::vector<std::string>{"effect", "model", "placement_set", "world"});
    std::vector<bundle_schema::Problem> problems = bundle_schema::lintSchemas();
    INFO(describe(problems));
    CHECK(problems.empty());
}

namespace {

// A placement set placing one instance of `effect`, written inline.
std::string effectSetDocument(const std::string& effect, const std::string& instance) {
    return std::string("{") + kHeader + R"(
  "kind": "placement_set",
  "subject": {"id": {"kind": "record_index", "value": 0}, "name": "Set_A", "name_source": "synthesized"},
  "definition": {}, "selection": {},
  "references": [{"kind": "effect", "asset": {"id": {"kind": "none"}, "name": "glow", "name_source": "synthesized"},
                  "document": )" + effect + R"(}],
  "instances": [)" + instance + R"(]
})";
}

std::string glowEffect(const std::string& status = R"("status": "placeholder",)") {
    return std::string("{") + kHeader + R"(
  "kind": "effect",
  "subject": {"id": {"kind": "none"}, "name": "glow", "name_source": "synthesized"},
  "definition": {)" + status + R"( "inputs": {
    "center": {"kind": "position", "description": "Centre."},
    "radius": {"kind": "number", "unit": "yard", "description": "Radius."}}},
  "selection": {}, "references": [],
  "sources": [{"id": "glow", "region": {"kind": "sphere", "center": {"input": "center"},
               "inner_radius": 0, "outer_radius": {"input": "radius"}}}]
})";
}

constexpr const char* kGlowInstance = R"({"id": 1, "reference": 0, "translation": [0, 0, 0], "rotation": [0, 0, 0, 1], "scale": 1,
  "inputs": {"center": [1, 2, 3], "radius": 4},
  "style": {"glow": {"distance_fog": {"linear": {"start": 1, "end": 10, "color": [1, 1, 1]}}}},
  "source": {"flags": "0x0", "record": {"table": "test", "index": 0}}})";

fs::path writeSet(const std::string& name, const std::string& set) {
    fs::path file = fs::temp_directory_path() / name / "set.json";
    fs::remove_all(file.parent_path());
    writeText(file, set);
    return file;
}

}  // namespace

TEST_CASE("bundle_schema: every effect husk writes validates") {
    for (const char* name : {"classic_local_fog", "classic_fog_exit_blend"}) {
        CAPTURE(name);
        fs::path file = fs::temp_directory_path() / "husk-schema-effects" / (std::string(name) + ".json");
        fs::create_directories(file.parent_path());
        writers::writeEffectDocument(name, file, writers::Provenance{"husk test", "test", {}});
        std::vector<bundle_schema::Problem> problems = bundle_schema::validateDocument(file);
        INFO(describe(problems));
        CHECK(problems.empty());
    }
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

    dir = writeBundle("husk-schema-units", replaced(model, R"("length_unit": "yard")", R"("length_unit": "metre")"), setDocument());
    CHECK_FALSE(bundle_schema::validateDocument(dir).empty());
}

TEST_CASE("bundle_schema: structural checks the schema can't express, each reporting expected and actual") {
    std::string model = modelDocument();

    fs::path dir = writeBundle("husk-schema-index", replaced(model, R"("material_index": 0)", R"("material_index": 1)"), setDocument());
    std::vector<bundle_schema::Problem> problems = bundle_schema::validateDocument(dir);
    CHECK(mentions(problems, "expected an index < 1 into '/resources/materials', got 1"));

    dir = writeBundle("husk-schema-length", replaced(model, R"("byte_length": 12)", R"("byte_length": 16)"), setDocument());
    CHECK(mentions(bundle_schema::validateDocument(dir), "expected byte_length 12"));

    dir = writeBundle("husk-schema-bounds", model, setDocument());
    writeText(dir / "mesh.bin", std::string(100, '\0'));
    CHECK(mentions(bundle_schema::validateDocument(dir), "within 'mesh.bin' (100 bytes)"));

    dir = writeBundle("husk-schema-escape", replaced(model, R"("file": "mesh.bin")", R"("file": "../mesh.bin")"), setDocument());
    CHECK(mentions(bundle_schema::validateDocument(dir), "inside the document's directory"));

    dir = writeBundle("husk-schema-set", model, replaced(setDocument(), R"("reference": 0)", R"("reference": 1)"));
    problems = bundle_schema::validateDocument(dir);
    CHECK(mentions(problems, "sets/0.json"));
    CHECK(mentions(problems, "expected an index < 1 into '/references', got 1"));

    dir = writeBundle("husk-schema-uri", replaced(model, R"("uri": "sets/0.json")", R"("uri": "sets/9.json")"), setDocument());
    CHECK(mentions(bundle_schema::validateDocument(dir), "expected 'sets/9.json' to exist"));
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
