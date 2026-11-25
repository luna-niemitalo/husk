// The anatomy family's checks beyond its schemas: REFACTOR/ANATOMY_EXCHANGE.md
// "What validate-bundle checks beyond the JSON Schema" is the list this
// implements. The schema has already held when these run.

#include <map>
#include <set>
#include <string>
#include <vector>

#include "bundle_schema_internal.hpp"

namespace husk::bundle_schema::internal {

namespace {

// Arrays of each kind whose entries are keyed by `id`.
const std::map<std::string, std::vector<std::string>> kIdArrays = {
    {"anatomy", {"/definition/tissues", "/definition/attachments", "/definition/rods", "/definition/parameters"}},
    {"anatomy_run", {"/definition/coordinates", "/selection/poses"}},
    {"anatomy_result", {"/definition/coordinates", "/poses"}},
};

void uniqueKeys(const json& array, const std::string& pointer, const char* key, const Fail& fail) {
    std::set<std::string> seen;
    for (size_t i = 0; i < array.size(); ++i) {
        std::string value = array[i][key];
        if (!seen.insert(value).second) {
            fail(pointer + "/" + std::to_string(i) + "/" + key, std::string("expected a unique ") + key + ", '" + value + "' repeats");
        }
    }
}

// A slice's element shape: the schema allows any, each use means one.
void sliceShape(const json& slice, const char* type, int components, const std::string& pointer, const Fail& fail) {
    if (slice["component_type"] != type || slice["component_count"] != components) {
        fail(pointer, std::string("expected ") + type + " x " + std::to_string(components) + ", got " +
                          slice["component_type"].get<std::string>() + " x " + slice["component_count"].dump());
    }
}

void sliceCount(const json& slice, size_t count, const char* of, const std::string& pointer, const Fail& fail) {
    if (slice["count"] != count) fail(pointer, "expected " + std::to_string(count) + " elements (one per " + of + "), got " + slice["count"].dump());
}

const json* skinTissue(const json& anatomy) {
    for (const json& t : anatomy["definition"]["tissues"]) {
        if (t["layer"] == "skin") return &t;
    }
    return nullptr;
}

void checkAnatomy(const json& anatomy, const Fail& fail) {
    const json& tissues = anatomy["definition"]["tissues"];
    size_t skins = 0;
    for (size_t i = 0; i < tissues.size(); ++i) {
        const json& t = tissues[i];
        std::string at = "/definition/tissues/" + std::to_string(i);
        if (t["layer"] == "skin") ++skins;
        sliceShape(t["surface"]["positions"], "f32", 3, at + "/surface/positions", fail);
        sliceShape(t["surface"]["triangles"], "u32", 3, at + "/surface/triangles", fail);
        bool muscle = t["layer"] == "muscle";
        if (!muscle) {
            for (const char* key : {"fibre_directions", "base_muscle"}) {
                if (t.contains(key)) fail(at + "/" + key, std::string("expected '") + key + "' only on a muscle, got it on " + t["layer"].get<std::string>());
            }
            continue;
        }
        if (!t.contains("fibre_directions")) {
            fail(at, "expected fibre_directions on a muscle, got none");
            continue;
        }
        sliceShape(t["fibre_directions"], "f32", 3, at + "/fibre_directions", fail);
        sliceCount(t["fibre_directions"], t["surface"]["positions"]["count"], "surface vertex", at + "/fibre_directions", fail);
    }
    if (skins != 1) fail("/definition/tissues", "expected exactly one skin tissue, got " + std::to_string(skins));
    const json& attachments = anatomy["definition"]["attachments"];
    for (size_t i = 0; i < attachments.size(); ++i) {
        sliceShape(attachments[i]["vertices"], "u32", 1, "/definition/attachments/" + std::to_string(i) + "/vertices", fail);
    }
}

// The run's or result's references: which one is the anatomy, which the
// run. A reference's id is its kind, and its uri is required.
struct AnatomyReferences {
    const json* anatomy = nullptr;
    const json* run = nullptr;
};

AnatomyReferences anatomyReferences(const std::string& kind, const json& document, const std::vector<const json*>& referenced,
                                    const Fail& fail) {
    AnatomyReferences out;
    std::map<std::string, size_t> counts;
    const json& refs = document["references"];
    for (size_t i = 0; i < refs.size(); ++i) {
        const json& r = refs[i];
        std::string at = "/references/" + std::to_string(i);
        std::string referencedKind = r["kind"];
        ++counts[referencedKind];
        if (r["id"] != referencedKind) fail(at + "/id", "expected the reference's kind '" + referencedKind + "' as its id, got " + r["id"].dump());
        if (!r["asset"].contains("uri")) fail(at + "/asset", "expected a uri: an anatomy-family reference names its document's file");
        const json*& slot = referencedKind == "anatomy" ? out.anatomy : out.run;
        slot = referenced[i];
    }
    if (counts["anatomy"] != 1) fail("/references", "expected exactly one anatomy reference, got " + std::to_string(counts["anatomy"]));
    size_t runsAllowed = kind == "anatomy_result" ? 1 : 0;
    if (counts["anatomy_run"] > runsAllowed) {
        fail("/references", "expected at most " + std::to_string(runsAllowed) + " anatomy_run reference, got " + std::to_string(counts["anatomy_run"]));
    }
    return out;
}

void checkPoseValues(const json& poses, const std::string& pointer, size_t coordinates, const Fail& fail) {
    for (size_t i = 0; i < poses.size(); ++i) {
        size_t got = poses[i]["values"].size();
        if (got != coordinates) {
            fail(pointer + "/" + std::to_string(i) + "/values",
                 "expected " + std::to_string(coordinates) + " values (one per coordinate), got " + std::to_string(got));
        }
    }
}

void checkBody(const json& body, const json* anatomy, const Fail& fail) {
    uniqueKeys(body, "/selection/body", "parameter", fail);
    if (!anatomy) return;
    std::set<std::string> parameters;
    for (const json& p : (*anatomy)["definition"]["parameters"]) parameters.insert(p["id"].get<std::string>());
    for (size_t i = 0; i < body.size(); ++i) {
        std::string name = body[i]["parameter"];
        if (!parameters.count(name)) fail("/selection/body/" + std::to_string(i) + "/parameter", "expected a parameter of the referenced anatomy, got '" + name + "'");
    }
}

void checkResult(const json& result, const AnatomyReferences& refs, const Fail& fail) {
    const json& skin = result["definition"]["skin"];
    size_t vertices = skin["vertex_count"];
    size_t triangles = skin["triangle_count"];
    if (const json* tissue = refs.anatomy ? skinTissue(*refs.anatomy) : nullptr) {
        if (skin["tissue"] != (*tissue)["id"]) fail("/definition/skin/tissue", "expected the anatomy's skin tissue " + (*tissue)["id"].dump() + ", got " + skin["tissue"].dump());
        if ((*tissue)["surface"]["positions"]["count"] != vertices) {
            fail("/definition/skin/vertex_count", "expected the anatomy's skin vertex count " + (*tissue)["surface"]["positions"]["count"].dump() + ", got " + std::to_string(vertices));
        }
        if ((*tissue)["surface"]["triangles"]["count"] != triangles) {
            fail("/definition/skin/triangle_count", "expected the anatomy's skin triangle count " + (*tissue)["surface"]["triangles"]["count"].dump() + ", got " + std::to_string(triangles));
        }
    }
    std::set<std::string> requested;
    if (refs.run) {
        for (const json& p : (*refs.run)["selection"]["poses"]) requested.insert(p["id"].get<std::string>());
    }
    const json& poses = result["poses"];
    bool anyFitError = false;
    for (size_t i = 0; i < poses.size(); ++i) {
        const json& p = poses[i];
        std::string at = "/poses/" + std::to_string(i);
        sliceShape(p["skin_positions"], "f32", 3, at + "/skin_positions", fail);
        sliceCount(p["skin_positions"], vertices, "skin vertex", at + "/skin_positions", fail);
        sliceShape(p["strain"], "f32", 5, at + "/strain", fail);
        sliceCount(p["strain"], triangles, "skin triangle", at + "/strain", fail);
        if (p.contains("fit_error")) {
            anyFitError = true;
            sliceShape(p["fit_error"], "f32", 1, at + "/fit_error", fail);
            sliceCount(p["fit_error"], vertices, "skin vertex", at + "/fit_error", fail);
        }
        std::string id = p["id"];
        if (refs.run && p["origin"] == "requested" && !requested.count(id)) {
            fail(at + "/id", "expected a requested pose to be one of the run's poses, got '" + id + "'");
        }
    }
    if (anyFitError && !result["solver"].contains("fit_reference")) {
        fail("/solver", "expected fit_reference: a pose has a fit_error, and the result must say what it is measured against");
    }
}

}  // namespace

bool isAnatomyKind(const std::string& kind) {
    return kIdArrays.count(kind) > 0;
}

void checkAnatomyFamily(const std::string& kind, const json& document, const std::vector<const json*>& referenced,
                        const std::set<std::string>& idChecked, const Fail& fail) {
    for (const std::string& pointer : kIdArrays.at(kind)) {
        if (idChecked.count(pointer)) continue;
        uniqueKeys(document.at(json::json_pointer(pointer)), pointer, "id", fail);
    }
    if (kind == "anatomy") {
        checkAnatomy(document, fail);
        return;
    }
    AnatomyReferences refs = anatomyReferences(kind, document, referenced, fail);
    size_t coordinates = document["definition"]["coordinates"].size();
    if (kind == "anatomy_run") {
        checkPoseValues(document["selection"]["poses"], "/selection/poses", coordinates, fail);
        checkBody(document["selection"]["body"], refs.anatomy, fail);
        return;
    }
    checkPoseValues(document["poses"], "/poses", coordinates, fail);
    checkBody(document["selection"]["body"], refs.anatomy, fail);
    checkResult(document, refs, fail);
}

}  // namespace husk::bundle_schema::internal
