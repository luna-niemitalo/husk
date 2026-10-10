#include "bundle_schema.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

// The validator's own nlohmann::json (3.12). Nothing in this file may
// include tinygltf or any husk header that does (see bundle_schema.hpp).
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include "bundle_schema_embedded.hpp"

namespace husk::bundle_schema {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

constexpr const char* kSchemaSuffix = ".schema.json";

const std::map<std::string, json>& schemas() {
    static const std::map<std::string, json> parsed = [] {
        std::map<std::string, json> out;
        for (const EmbeddedSchema& s : embeddedSchemas()) out.emplace(std::string(s.name), json::parse(s.text));
        return out;
    }();
    return parsed;
}

// "common.schema.json" or a full https://husk.invalid/schema/... uri -> "common".
std::string schemaStem(const std::string& location) {
    std::string file = location.substr(location.find_last_of('/') + 1);
    if (file.size() > std::string(kSchemaSuffix).size() && file.ends_with(kSchemaSuffix)) {
        return file.substr(0, file.size() - std::string(kSchemaSuffix).size());
    }
    return file;
}

const json& schemaFor(const std::string& stem) {
    auto it = schemas().find(stem);
    if (it == schemas().end()) throw std::runtime_error("no embedded schema '" + stem + "'");
    return it->second;
}

// ---------------------------------------------------------------- lint

// Words that name a storage container or a runtime API. The format links
// data and states its media type; it never has an opinion on either.
const std::set<std::string> kStorageWords = {
    "dds", "png", "blp", "jpg", "jpeg", "tga", "ktx", "ktx2", "webp", "exr", "bmp",
    "glb", "gltf", "fbx", "obj", "usd", "usdz",
    "vk", "vulkan", "d3d", "d3d11", "d3d12", "dx11", "dx12", "opengl", "gl", "metal", "bpy", "blender",
};

// Source-format chunk and file names. A key containing one leaks the input
// format into the interchange format; raw values go under `source`.
const std::set<std::string> kSourceJargon = {
    "m2", "wmo", "adt", "wdt", "skel", "phys", "md20", "md21", "sfid", "txid", "skid", "bfid", "afid",
    "mver", "mohd", "momt", "motx", "mogn", "mogi", "mods", "modn", "modi", "modd", "mddi", "mfog", "mfed",
    "mfob", "mavg", "mavd", "mbvd", "mpvd", "mnld", "molt", "mols", "molp", "mouv", "mosb", "mosi", "mopv",
    "mopt", "mopr", "mope", "movv", "movb", "mogp", "mopy", "mpy2", "movi", "movx", "movt", "monr", "motv",
    "mocv", "moba", "mliq", "mobn", "mobr", "mcvp", "mopl", "gfid", "mh2o", "mcnk", "mddf", "modf", "mmdx",
    "mmid", "mwmo", "mwid", "mtex", "mcvt", "mcnr", "mcly", "mcal", "mwdr", "mwds", "vfog", "vfex", "vfe2",
    "pvmi", "pvpd", "pvbd",
};

const std::set<std::string> kLayers = {"contract", "definition", "selection", "resource"};

const std::vector<std::string> kHeader = {"format", "kind", "schema_version", "subject", "space",
                                          "provenance", "definition", "selection", "references"};

// Definitions that are real-valued and so need a unit where they are used.
const std::set<std::string> kRealValuedDefinitions = {"vec2", "vec3", "vec4", "quat", "rgb", "rgba",
                                                      "animated", "lifetime_curve", "inline_curve"};

// Keys that hold file paths: only the shared link definitions may have them.
const std::set<std::string> kPathKeys = {"uri", "file"};
const std::set<std::string> kPathDefinitions = {"asset_ref", "payload", "buffer_slice"};

std::vector<std::string> tokens(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            cur += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (!cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::string refTarget(const json& node) {
    if (!node.is_object() || !node.contains("$ref")) return {};
    std::string ref = node["$ref"];
    return ref.substr(ref.find_last_of('/') + 1);
}

bool isRealValued(const json& node) {
    if (kRealValuedDefinitions.count(refTarget(node))) return true;
    if (!node.is_object()) return false;
    if (node.value("type", "") == "number") return true;
    if (node.value("type", "") == "array" && node.contains("items")) {
        const json& items = node["items"];
        return items.value("type", "") == "number" || kRealValuedDefinitions.count(refTarget(items)) > 0;
    }
    return false;
}

// Follows a schema node through a property path, resolving $refs, to see
// whether an x-index-into pointer names an array the document can hold.
const json* followSchema(const std::string& stem, const json* node, const std::vector<std::string>& path) {
    for (const std::string& step : path) {
        for (int hops = 0; node && node->contains("$ref") && hops < 8; ++hops) {
            std::string ref = (*node)["$ref"];
            size_t hash = ref.find('#');
            std::string target = hash == 0 ? stem : schemaStem(ref.substr(0, hash));
            node = &schemaFor(target).at(json::json_pointer(ref.substr(hash + 1)));
        }
        if (!node) return nullptr;
        if (node->value("type", "") == "array" && node->contains("items")) node = &(*node)["items"];
        if (!node->contains("properties") || !(*node)["properties"].contains(step)) return nullptr;
        node = &(*node)["properties"][step];
    }
    return node;
}

std::vector<std::string> splitPointer(const std::string& pointer) {
    std::vector<std::string> out;
    std::stringstream ss(pointer.substr(1));
    for (std::string part; std::getline(ss, part, '/');) out.push_back(part);
    return out;
}

struct Linter {
    std::string stem;
    const json& root;
    std::vector<Problem>& problems;

    void fail(const std::string& pointer, const std::string& message) {
        problems.push_back({stem + kSchemaSuffix + "#" + pointer, message});
    }

    void checkWords(const std::string& pointer, const std::string& text, const char* what) {
        for (const std::string& t : tokens(text)) {
            if (kStorageWords.count(t)) fail(pointer, std::string(what) + " '" + text + "' names a storage container or API ('" + t + "'); link data with a payload and its media type instead");
        }
    }

    // `objects` are the enclosing object schemas, innermost last; `definition`
    // is the common definition being walked, empty inside a kind schema.
    void walk(const json& node, const std::string& pointer, std::vector<const json*>& objects, const std::string& definition) {
        if (!node.is_object()) return;
        if (node.contains("enum")) {
            for (const json& v : node["enum"]) if (v.is_string()) checkWords(pointer + "/enum", v, "enum value");
        }
        if (node.contains("const") && node["const"].is_string()) checkWords(pointer + "/const", node["const"], "constant");
        if (node.contains("x-index-into")) checkIndex(pointer, node["x-index-into"], objects);
        if (node.contains("properties")) {
            if (node.value("additionalProperties", true) != false) {
                fail(pointer, "object is open: set additionalProperties to false so an undocumented key fails validation");
            }
            objects.push_back(&node);
            for (const auto& [key, sub] : node["properties"].items()) checkProperty(pointer + "/properties/" + key, key, sub, objects, definition);
            objects.pop_back();
        }
        if (node.contains("items")) walk(node["items"], pointer + "/items", objects, definition);
        for (const char* combinator : {"oneOf", "anyOf", "allOf"}) {
            if (!node.contains(combinator)) continue;
            for (size_t i = 0; i < node[combinator].size(); ++i) {
                walk(node[combinator][i], pointer + "/" + combinator + "/" + std::to_string(i), objects, definition);
            }
        }
    }

    // Set while walking the direct properties of a `source`/`unresolved` object.
    bool rawParent = false;

    void checkProperty(const std::string& pointer, const std::string& key, const json& sub, std::vector<const json*>& objects, const std::string& definition) {
        static const std::regex snake("^[a-z][a-z0-9_]*$");
        if (!std::regex_match(key, snake)) fail(pointer, "key '" + key + "' is not lowercase snake_case");
        for (const std::string& t : tokens(key)) {
            if (kSourceJargon.count(t)) fail(pointer, "key '" + key + "' contains source-format name '" + t + "'; describe the meaning, and put raw values under 'source'");
        }
        checkWords(pointer, key, "key");
        if (kPathKeys.count(key) && !kPathDefinitions.count(definition)) {
            fail(pointer, "key '" + key + "' holds a path outside the shared link definitions (asset_ref, payload, buffer_slice)");
        }
        if (!rawParent && (key.starts_with("source_") || key.starts_with("unknown") || key.starts_with("raw"))) {
            fail(pointer, "key '" + key + "' is raw source data; put it under a 'source' or 'unresolved' object");
        }
        if (!sub.is_object() || !sub.contains("description")) fail(pointer, "property '" + key + "' has no description");
        if (isRealValued(sub) && !sub.contains("x-unit")) fail(pointer, "real-valued property '" + key + "' has no x-unit");
        bool savedRaw = rawParent;
        rawParent = key == "source" || key == "unresolved";
        walk(sub, pointer, objects, definition);
        rawParent = savedRaw;
    }

    void checkIndex(const std::string& pointer, const json& target, const std::vector<const json*>& objects) {
        std::string t = target;
        if (t.starts_with("/")) {
            const json* found = followSchema(stem, &root, splitPointer(t));
            if (!found) {
                fail(pointer, "x-index-into '" + t + "' names nothing in the " + stem + " schema");
            } else if (found->value("type", "") != "array") {
                fail(pointer, "x-index-into '" + t + "' names a non-array");
            }
            return;
        }
        bool found = std::any_of(objects.begin(), objects.end(), [&](const json* o) { return (*o)["properties"].contains(t); });
        if (!found) fail(pointer, "x-index-into '" + t + "' names no property of an enclosing object");
    }
};

void lintKindHeader(const std::string& stem, const json& schema, std::vector<Problem>& problems) {
    auto fail = [&](const std::string& message) { problems.push_back({stem + kSchemaSuffix, message}); };
    const json& props = schema.value("properties", json::object());
    for (const std::string& key : kHeader) {
        if (!props.contains(key)) fail("missing contract header property '" + key + "'");
    }
    std::vector<std::string> required = schema.value("required", std::vector<std::string>{});
    for (const std::string& key : kHeader) {
        if (std::find(required.begin(), required.end(), key) == required.end()) fail("contract header property '" + key + "' is not required");
    }
    for (const auto& [key, sub] : props.items()) {
        if (!sub.contains("x-layer") || !kLayers.count(sub["x-layer"].get<std::string>())) {
            fail("top-level property '" + key + "' has no x-layer of contract/definition/selection/resource");
        }
    }
    if (schema.value("x-status", "") != "implemented") fail("x-status must be 'implemented'");
}

// ---------------------------------------------------------------- validation

struct CollectingHandler : nlohmann::json_schema::error_handler {
    std::string where;
    std::vector<Problem>& problems;
    CollectingHandler(std::string w, std::vector<Problem>& p) : where(std::move(w)), problems(p) {}
    void error(const json::json_pointer& ptr, const json&, const std::string& message) override {
        problems.push_back({where + "#" + ptr.to_string(), message});
    }
};

void loadEmbedded(const nlohmann::json_uri& uri, json& out) {
    out = schemaFor(schemaStem(uri.path()));
}

size_t componentSize(const std::string& type) {
    if (type == "u8") return 1;
    if (type == "u16") return 2;
    return 4;  // u32, i32, f32: the schema closes the vocabulary
}

bool escapes(const std::string& relative) {
    fs::path p(relative);
    return p.is_absolute() || std::any_of(p.begin(), p.end(), [](const fs::path& part) { return part == ".."; });
}

struct DocumentChecker {
    fs::path file;
    const json& document;
    std::vector<Problem>& problems;
    std::vector<fs::path> placementSets;

    void fail(const std::string& pointer, const std::string& message) {
        problems.push_back({file.string() + "#" + pointer, message});
    }

    const json* resolveRef(std::string& stem, const json* node) {
        for (int hops = 0; node->is_object() && node->contains("$ref") && hops < 8; ++hops) {
            std::string ref = (*node)["$ref"];
            size_t hash = ref.find('#');
            if (hash != 0) stem = schemaStem(ref.substr(0, hash));
            node = &schemaFor(stem).at(json::json_pointer(ref.substr(hash + 1)));
        }
        return node;
    }

    // Walks instance and schema together. `objects` are the enclosing
    // instance objects, innermost last, for relative x-index-into names.
    void walk(const json& value, const json* schema, std::string stem, const std::string& pointer,
              std::vector<const json*>& objects) {
        std::string refName = refTarget(*schema);
        if (schema->contains("x-index-into")) checkIndex(value, (*schema)["x-index-into"], pointer, objects);
        schema = resolveRef(stem, schema);
        if (refName == "buffer_slice") checkSlice(value, pointer);
        if ((refName == "payload" || refName == "asset_ref") && value.contains("uri")) checkUri(value["uri"], pointer + "/uri");
        if (value.is_object() && schema->contains("properties")) {
            objects.push_back(&value);
            for (const auto& [key, sub] : (*schema)["properties"].items()) {
                if (value.contains(key)) walk(value[key], &sub, stem, pointer + "/" + key, objects);
            }
            objects.pop_back();
        } else if (value.is_array() && schema->contains("items")) {
            for (size_t i = 0; i < value.size(); ++i) walk(value[i], &(*schema)["items"], stem, pointer + "/" + std::to_string(i), objects);
        }
    }

    void checkIndex(const json& value, const std::string& target, const std::string& pointer, const std::vector<const json*>& objects) {
        if (!value.is_number_integer()) return;
        const json* array = nullptr;
        if (target.starts_with("/")) {
            json::json_pointer p(target);
            if (document.contains(p)) array = &document.at(p);
        } else {
            for (auto it = objects.rbegin(); it != objects.rend() && !array; ++it) {
                if ((*it)->contains(target)) array = &(**it)[target];
            }
        }
        if (!array || !array->is_array()) {
            fail(pointer, "expected an index into '" + target + "', which the document doesn't hold");
            return;
        }
        auto index = value.get<int64_t>();
        if (index < 0 || static_cast<size_t>(index) >= array->size()) {
            fail(pointer, "expected an index < " + std::to_string(array->size()) + " into '" + target + "', got " + std::to_string(index));
        }
    }

    void checkSlice(const json& slice, const std::string& pointer) {
        size_t count = slice["count"];
        size_t components = slice["component_count"];
        size_t length = slice["byte_length"];
        size_t offset = slice["byte_offset"];
        size_t expected = count * components * componentSize(slice["component_type"]);
        if (length != expected) {
            fail(pointer, "expected byte_length " + std::to_string(expected) + " (count * component_count * component size), got " + std::to_string(length));
        }
        std::string name = slice["file"];
        if (escapes(name)) {
            fail(pointer + "/file", "expected a path inside the document's directory, got '" + name + "'");
            return;
        }
        fs::path bin = file.parent_path() / name;
        std::error_code ec;
        uintmax_t size = fs::file_size(bin, ec);
        if (ec) {
            fail(pointer + "/file", "expected file '" + name + "' to exist next to the document");
        } else if (offset + length > size) {
            fail(pointer, "expected bytes [" + std::to_string(offset) + ", " + std::to_string(offset + length) + ") within '" + name + "' (" + std::to_string(size) + " bytes)");
        }
    }

    void checkUri(const std::string& uri, const std::string& pointer) {
        if (fs::path(uri).is_absolute()) {
            fail(pointer, "expected a relative uri, got '" + uri + "'");
            return;
        }
        if (!fs::exists(file.parent_path() / uri)) fail(pointer, "expected '" + uri + "' to exist relative to the document");
    }

    void collectPlacementSets() {
        const json& refs = document.value("references", json::array());
        for (const json& r : refs) {
            if (r.value("kind", "") != "placement_set") continue;
            const json& asset = r["asset"];
            if (asset.contains("uri")) placementSets.push_back(file.parent_path() / asset["uri"].get<std::string>());
        }
    }
};

void validateFile(const fs::path& file, std::vector<Problem>& problems, std::vector<fs::path>& checked) {
    fs::path canonical = fs::weakly_canonical(file);
    if (std::find(checked.begin(), checked.end(), canonical) != checked.end()) return;
    checked.push_back(canonical);

    std::ifstream in(file, std::ios::binary);
    if (!in) {
        problems.push_back({file.string(), "expected a readable document file"});
        return;
    }
    json document = json::parse(in, nullptr, false);
    if (document.is_discarded()) {
        problems.push_back({file.string(), "expected JSON"});
        return;
    }
    std::string kind = document.value("kind", "");
    if (!schemas().count(kind) || kind == "common") {
        problems.push_back({file.string() + "#/kind", "expected one of the document kinds schema/ defines, got '" + kind + "'"});
        return;
    }

    size_t before = problems.size();
    nlohmann::json_schema::json_validator validator(loadEmbedded);
    validator.set_root_schema(schemaFor(kind));
    CollectingHandler handler(file.string(), problems);
    try {
        validator.validate(document, handler);
    } catch (const std::exception& e) {
        problems.push_back({file.string(), std::string("schema validation could not run: ") + e.what()});
    }
    // The structural walk assumes the schema holds; past a schema failure it
    // would only report the same problem again in a worse form.
    if (problems.size() != before) return;

    DocumentChecker checker{file, document, problems, {}};
    std::vector<const json*> objects;
    checker.walk(document, &schemaFor(kind), kind, "", objects);
    checker.collectPlacementSets();
    for (const fs::path& set : checker.placementSets) validateFile(set, problems, checked);
}

}  // namespace

std::vector<std::string> kinds() {
    std::vector<std::string> out;
    for (const auto& [stem, schema] : schemas()) {
        if (stem != "common") out.push_back(stem);
    }
    return out;
}

void lintOne(const std::string& stem, const json& schema, std::vector<Problem>& problems) {
    Linter linter{stem, schema, problems};
    std::vector<const json*> objects;
    if (stem == "common") {
        for (const auto& [name, def] : schema["definitions"].items()) {
            std::string pointer = "/definitions/" + name;
            if (!def.contains("description")) problems.push_back({stem + kSchemaSuffix + "#" + pointer, "definition '" + name + "' has no description"});
            linter.walk(def, pointer, objects, name);
        }
        return;
    }
    lintKindHeader(stem, schema, problems);
    linter.walk(schema, "", objects, "");
}

std::vector<Problem> lintSchemas() {
    std::vector<Problem> problems;
    for (const auto& [stem, schema] : schemas()) lintOne(stem, schema, problems);
    return problems;
}

std::vector<Problem> lintSchemaText(const std::string& stem, const std::string& text) {
    std::vector<Problem> problems;
    lintOne(stem, json::parse(text), problems);
    return problems;
}

std::vector<Problem> validateDocument(const fs::path& path, std::vector<fs::path>* checked) {
    std::vector<Problem> problems;
    std::vector<fs::path> seen;
    fs::path file = fs::is_directory(path) ? path / "manifest.json" : path;
    validateFile(file, problems, seen);
    if (checked) *checked = seen;
    return problems;
}

}  // namespace husk::bundle_schema
