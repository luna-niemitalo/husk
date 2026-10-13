#include "bundle_schema.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
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
                                                      "animated", "lifetime_curve", "inline_curve",
                                                      "bound_number", "bound_vec3"};

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
        // A shared definition's absolute target names a place in whichever
        // document uses it, so only validation can check it.
        if (t.starts_with("/") && stem == "common") return;
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

// `prefix` is where the document sits inside its file: empty for a file's
// own document, a JSON pointer for an inline one.
struct CollectingHandler : nlohmann::json_schema::error_handler {
    std::string where;
    std::string prefix;
    std::vector<Problem>& problems;
    CollectingHandler(std::string w, std::string p, std::vector<Problem>& out)
        : where(std::move(w)), prefix(std::move(p)), problems(out) {}
    void error(const json::json_pointer& ptr, const json&, const std::string& message) override {
        problems.push_back({where + "#" + prefix + ptr.to_string(), message});
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
    std::string prefix;  // see CollectingHandler
    const json& document;
    std::vector<Problem>& problems;

    void fail(const std::string& pointer, const std::string& message) {
        problems.push_back({file.string() + "#" + prefix + pointer, message});
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

};

// ---------------------------------------------------------------- effects

// The same rule the lint applies to schema keys: an input name is part of
// the format a consumer reads, so it may not leak the source format.
std::string nameProblem(const std::string& name) {
    static const std::regex snake("^[a-z][a-z0-9_]*$");
    if (!std::regex_match(name, snake)) return "'" + name + "' is not lowercase snake_case";
    for (const std::string& t : tokens(name)) {
        if (kSourceJargon.count(t)) return "'" + name + "' contains source-format name '" + t + "'";
    }
    return {};
}

std::string describeValue(const json& value) {
    std::string text = value.dump();
    return text.size() > 60 ? text.substr(0, 57) + "..." : text;
}

bool isNumbers(const json& value, size_t count) {
    return value.is_array() && value.size() == count &&
           std::all_of(value.begin(), value.end(), [](const json& v) { return v.is_number(); });
}

// One value against one input spec. The schema has already checked the
// specs themselves.
struct InputChecker {
    std::function<void(const std::string& pointer, const std::string& message)> fail;
    size_t textureCount = 0;  // entries in the document's resources.textures

    void check(const json& spec, const json& value, const std::string& pointer, const std::string& name) {
        std::string kind = spec["kind"];
        auto expect = [&](bool ok, const std::string& shape) {
            if (!ok) fail(pointer, "input '" + name + "' is a " + kind + ": expected " + shape + ", got " + describeValue(value));
            return ok;
        };
        if (kind == "number") {
            expect(value.is_number(), "a number");
        } else if (kind == "integer") {
            expect(value.is_number_integer(), "an integer");
        } else if (kind == "boolean") {
            expect(value.is_boolean(), "true or false");
        } else if (kind == "choice") {
            std::vector<std::string> options = spec.value("options", std::vector<std::string>{});
            expect(value.is_string() && std::find(options.begin(), options.end(), value.get<std::string>()) != options.end(),
                   "one of " + json(options).dump());
        } else if (kind == "position" || kind == "vector" || kind == "color") {
            expect(isNumbers(value, 3), "3 numbers");
        } else if (kind == "direction") {
            if (!expect(isNumbers(value, 3), "3 numbers of unit length")) return;
            double length = std::sqrt(value[0].get<double>() * value[0].get<double>() +
                                      value[1].get<double>() * value[1].get<double>() +
                                      value[2].get<double>() * value[2].get<double>());
            expect(std::abs(length - 1.0) < 1e-3, "3 numbers of unit length");
        } else if (kind == "rotation" || kind == "color_alpha") {
            expect(isNumbers(value, 4), "4 numbers");
        } else if (kind == "image") {
            expect(value.is_number_unsigned() && value.get<size_t>() < textureCount,
                   "an index into resources.textures (" + std::to_string(textureCount) + " entries)");
        } else if (kind == "mesh") {
            expect(value.is_object() && value.contains("id"), "an asset reference");
        } else if (kind == "spline") {
            expect(value.is_array() && std::all_of(value.begin(), value.end(), [](const json& p) { return isNumbers(p, 3); }),
                   "a list of positions");
        } else if (kind == "curve") {
            expect(value.is_array(), "a list of keyed values");
        } else if (kind == "list") {
            if (!expect(value.is_array(), "a list")) return;
            for (size_t i = 0; i < value.size(); ++i) check(spec["item"], value[i], pointer + "/" + std::to_string(i), name + "[" + std::to_string(i) + "]");
        } else if (kind == "record") {
            if (!expect(value.is_object(), "an object")) return;
            checkFields(spec.value("fields", json::object()), value, pointer, name + ".");
        }
    }

    // `values` holds what an instance or record supplies for `specs`.
    void checkFields(const json& specs, const json& values, const std::string& pointer, const std::string& namePrefix) {
        for (const auto& [name, value] : values.items()) {
            if (!specs.contains(name)) {
                fail(pointer + "/" + name, "expected only the inputs the effect declares (" + describeValue(json(keysOf(specs))) + "), got '" + namePrefix + name + "'");
                continue;
            }
            check(specs[name], value, pointer + "/" + name, namePrefix + name);
        }
        for (const auto& [name, spec] : specs.items()) {
            if (values.contains(name) || spec.value("optional", false) || spec.contains("default")) continue;
            fail(pointer, "expected input '" + namePrefix + name + "' (" + spec["kind"].get<std::string>() + "), which has no default, got none");
        }
    }

    static std::vector<std::string> keysOf(const json& object) {
        std::vector<std::string> out;
        for (const auto& [key, _] : object.items()) out.push_back(key);
        return out;
    }
};

// Every binding inside a source: {"input": name} or {"each": field}.
void collectBindings(const json& node, const std::string& pointer, std::vector<std::pair<std::string, const json*>>& out) {
    if (node.is_object()) {
        if ((node.contains("input") || node.contains("each")) && node.size() == 1) {
            out.emplace_back(pointer, &node);
            return;
        }
        for (const auto& [key, sub] : node.items()) collectBindings(sub, pointer + "/" + key, out);
    } else if (node.is_array()) {
        for (size_t i = 0; i < node.size(); ++i) collectBindings(node[i], pointer + "/" + std::to_string(i), out);
    }
}

// What a schema can't say about a technique document: each expression
// node is exactly one of input, term, const or op; an op has the operand
// count it takes; and every input and term it names is declared.
void checkExpression(const json& node, const json& definition, const std::string& pointer,
                     const std::function<void(const std::string&, const std::string&)>& fail) {
    int kinds = node.contains("input") + node.contains("term") + node.contains("const") + node.contains("op");
    if (kinds != 1) {
        fail(pointer, "expected exactly one of input, term, const, op, got " + std::to_string(kinds));
        return;
    }
    if (node.contains("input") && !definition["inputs"].contains(node["input"].get<std::string>())) {
        fail(pointer + "/input", "expected an input definition.inputs declares, got '" + node["input"].get<std::string>() + "'");
    }
    if (node.contains("term") && !definition.value("terms", json::object()).contains(node["term"].get<std::string>())) {
        fail(pointer + "/term", "expected a term definition.terms declares, got '" + node["term"].get<std::string>() + "'");
    }
    if (node.contains("args") && !node.contains("op")) fail(pointer + "/args", "expected args only on an op");
    if (node.contains("channels") && !node.contains("input") && node.value("op", "") != "channels") {
        fail(pointer + "/channels", "expected channels only on an input or a channels op");
    }
    if (!node.contains("op")) return;
    static const std::map<std::string, std::pair<size_t, size_t>> arity = {
        {"add", {2, 99}}, {"sub", {2, 2}}, {"mul", {2, 99}}, {"lerp", {3, 3}},
        {"saturate", {1, 1}}, {"luma", {1, 1}}, {"channels", {1, 1}}, {"lit", {1, 1}}};
    std::string op = node["op"];
    size_t n = node.contains("args") ? node["args"].size() : 0;
    auto [lo, hi] = arity.at(op);
    if (n < lo || n > hi) fail(pointer + "/args", "expected " + std::to_string(lo) + (hi == lo ? "" : "+") + " operands for " + op + ", got " + std::to_string(n));
    if (op == "channels" && !node.contains("channels")) fail(pointer, "expected channels on a channels op");
    for (size_t i = 0; i < n; ++i) checkExpression(node["args"][i], definition, pointer + "/args/" + std::to_string(i), fail);
}

void checkTechnique(const json& technique, const std::function<void(const std::string&, const std::string&)>& fail) {
    const json& definition = technique["definition"];
    for (const char* section : {"terms", "outputs"}) {
        if (!definition.contains(section)) continue;
        for (const auto& [name, expr] : definition[section].items()) {
            checkExpression(expr, definition, std::string("/definition/") + section + "/" + name, fail);
        }
    }
}

// What a schema can't say about an effect document: input names, that
// every binding names a declared input and every input is bound, and that
// source ids and parents hold together.
void checkEffect(const json& effect, const std::function<void(const std::string&, const std::string&)>& fail) {
    const json& inputs = effect["definition"]["inputs"];
    for (const auto& [name, spec] : inputs.items()) {
        if (std::string problem = nameProblem(name); !problem.empty()) fail("/definition/inputs/" + name, "input name " + problem);
    }
    std::set<std::string> bound;
    std::set<std::string> ids;
    const json& sources = effect["sources"];
    for (size_t i = 0; i < sources.size(); ++i) {
        const json& source = sources[i];
        std::string pointer = "/sources/" + std::to_string(i);
        std::string id = source["id"];
        if (!ids.insert(id).second) fail(pointer + "/id", "expected source ids unique within the effect, got '" + id + "' twice");
        const json* element = nullptr;
        if (source.contains("for_each")) {
            std::string list = source["for_each"];
            bound.insert(list);
            if (!inputs.contains(list) || inputs[list]["kind"] != "list") {
                fail(pointer + "/for_each", "expected a list input, got '" + list + "'");
            } else {
                element = &inputs[list]["item"];
            }
        }
        std::vector<std::pair<std::string, const json*>> bindings;
        collectBindings(source, pointer, bindings);
        for (const auto& [at, binding] : bindings) {
            if (binding->contains("input")) {
                std::string name = (*binding)["input"];
                bound.insert(name);
                if (!inputs.contains(name)) fail(at, "expected a binding to a declared input, got '" + name + "'");
                continue;
            }
            std::string field = (*binding)["each"];
            if (!element) {
                fail(at, "expected 'each' only inside a for_each source, got '" + field + "'");
            } else if (!element->value("fields", json::object()).contains(field)) {
                fail(at, "expected a field of the for_each element, got '" + field + "'");
            }
        }
    }
    for (size_t i = 0; i < sources.size(); ++i) {
        if (!sources[i].contains("parent")) continue;
        std::string parent = sources[i]["parent"];
        if (!ids.count(parent)) fail("/sources/" + std::to_string(i) + "/parent", "expected the id of another source, got '" + parent + "'");
    }
    for (const auto& [name, spec] : inputs.items()) {
        if (!bound.count(name)) fail("/definition/inputs/" + name, "expected input '" + name + "' to be bound by a source; an input nothing reads is an unresolved field");
    }
}

// ---------------------------------------------------------------- traversal

struct Validation {
    std::vector<Problem>& problems;
    std::vector<Problem>* warnings;
    std::vector<fs::path>& checked;
    std::map<fs::path, json> files;  // parsed documents by canonical path

    // A document by file: parsed once, validated once. Null when it can't be read.
    const json* file(const fs::path& path) {
        fs::path canonical = fs::weakly_canonical(path);
        if (auto it = files.find(canonical); it != files.end()) return it->second.is_discarded() ? nullptr : &it->second;
        checked.push_back(canonical);
        json& document = files[canonical];
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            document = json::value_t::discarded;
            problems.push_back({path.string(), "expected a readable document file"});
            return nullptr;
        }
        document = json::parse(in, nullptr, false);
        if (document.is_discarded()) {
            problems.push_back({path.string(), "expected JSON"});
            return nullptr;
        }
        if (!validate(document, path, "")) return nullptr;
        return &document;
    }

    // One document, at `prefix` inside `path`. False when it fails its schema.
    bool validate(const json& document, const fs::path& path, const std::string& prefix) {
        std::string kind = document.value("kind", "");
        if (!schemas().count(kind) || kind == "common") {
            problems.push_back({path.string() + "#" + prefix + "/kind", "expected one of the document kinds schema/ defines, got '" + kind + "'"});
            return false;
        }
        size_t before = problems.size();
        nlohmann::json_schema::json_validator validator(loadEmbedded);
        validator.set_root_schema(schemaFor(kind));
        CollectingHandler handler(path.string(), prefix, problems);
        try {
            validator.validate(document, handler);
        } catch (const std::exception& e) {
            problems.push_back({path.string(), std::string("schema validation could not run: ") + e.what()});
        }
        // The structural walk assumes the schema holds; past a schema failure it
        // would only report the same problem again in a worse form.
        if (problems.size() != before) return false;

        DocumentChecker checker{path, prefix, document, problems};
        std::vector<const json*> objects;
        checker.walk(document, &schemaFor(kind), kind, "", objects);
        if (kind == "effect") checkEffect(document, [&](const std::string& p, const std::string& m) { checker.fail(p, m); });
        if (kind == "technique") checkTechnique(document, [&](const std::string& p, const std::string& m) { checker.fail(p, m); });

        if (kind == "material") checkMaterialInputs(document, checker);
        std::vector<const json*> referenced = references(document, path, prefix);
        if (kind == "placement_set") checkEffectInstances(document, referenced, checker);
        if (kind == "model") checkMaterialBindings(document, referenced, checker);
        return true;
    }

    // Every {"input": name} a material's layers or composites read names an
    // input its definition declares.
    void checkMaterialInputs(const json& material, DocumentChecker& checker) {
        const json& inputs = material["definition"]["inputs"];
        auto check = [&](const json& source, const std::string& pointer) {
            if (!source.is_object() || inputs.contains(source["input"].get<std::string>())) return;
            checker.fail(pointer, "expected an input the material declares, got '" + source["input"].get<std::string>() + "'");
        };
        const json& layers = material["layers"];
        for (size_t i = 0; i < layers.size(); ++i) check(layers[i]["texture"], "/layers/" + std::to_string(i) + "/texture");
        const json& textures = material["resources"]["textures"];
        for (size_t t = 0; t < textures.size(); ++t) {
            if (!textures[t].contains("composite")) continue;
            const json& drawn = textures[t]["composite"]["layers"];
            for (size_t i = 0; i < drawn.size(); ++i) {
                check(drawn[i]["texture"], "/resources/textures/" + std::to_string(t) + "/composite/layers/" + std::to_string(i) + "/texture");
            }
        }
    }

    // A model's slot composites and choice textures name material
    // references, and each bound input is one that material declares.
    void checkMaterialBindings(const json& model, const std::vector<const json*>& referenced, DocumentChecker& checker) {
        const json& refs = model["references"];
        auto material = [&](const json& index, const std::string& pointer) -> const json* {
            size_t i = index;
            if (i >= refs.size()) return nullptr;  // the index check reports it
            if (refs[i]["kind"] != "material") {
                checker.fail(pointer, "expected a material reference, got a " + refs[i]["kind"].get<std::string>());
                return nullptr;
            }
            return referenced[i];
        };
        const json& definition = model["definition"];
        const json& slots = definition.value("texture_slots", json::array());
        for (size_t s = 0; s < slots.size(); ++s) {
            if (slots[s].contains("composite")) material(slots[s]["composite"], "/definition/texture_slots/" + std::to_string(s) + "/composite");
        }
        if (!definition.contains("customization")) return;
        const json& options = definition["customization"]["options"];
        for (size_t o = 0; o < options.size(); ++o) {
            const json& choices = options[o]["choices"];
            for (size_t c = 0; c < choices.size(); ++c) {
                const json& bound = choices[c].value("textures", json::array());
                for (size_t b = 0; b < bound.size(); ++b) {
                    std::string pointer = "/definition/customization/options/" + std::to_string(o) + "/choices/" +
                                          std::to_string(c) + "/textures/" + std::to_string(b);
                    const json* target = material(bound[b]["material"], pointer + "/material");
                    if (!target) continue;
                    std::string input = bound[b]["input"];
                    if (!(*target)["definition"]["inputs"].contains(input)) {
                        checker.fail(pointer + "/input", "expected an input the material declares, got '" + input + "'");
                    }
                }
            }
        }
    }

    // Validates every placement set, effect and material the document
    // references and returns each reference's document (null for other
    // kinds and failures).
    std::vector<const json*> references(const json& document, const fs::path& path, const std::string& prefix) {
        std::vector<const json*> out;
        // Every kind's schema requires references; out keeps pointers into it.
        const json& refs = document["references"];
        for (size_t i = 0; i < refs.size(); ++i) {
            const json& r = refs[i];
            std::string kind = r.value("kind", "");
            if (kind != "placement_set" && kind != "effect" && kind != "material" && kind != "technique") {
                out.push_back(nullptr);
                continue;
            }
            if (r.contains("document")) {
                bool ok = validate(r["document"], path, prefix + "/references/" + std::to_string(i) + "/document");
                out.push_back(ok ? &r["document"] : nullptr);
            } else if (r["asset"].contains("uri")) {
                out.push_back(file(path.parent_path() / r["asset"]["uri"].get<std::string>()));
            } else {
                out.push_back(nullptr);
            }
        }
        return out;
    }

    void checkEffectInstances(const json& set, const std::vector<const json*>& referenced, DocumentChecker& checker) {
        const json& instances = set["instances"];
        for (size_t i = 0; i < instances.size(); ++i) {
            const json& instance = instances[i];
            std::string pointer = "/instances/" + std::to_string(i);
            size_t reference = instance["reference"];
            if (reference >= referenced.size()) continue;  // the index check has reported it
            if (set["references"][reference]["kind"] != "effect") {
                for (const char* key : {"inputs", "style", "unresolved"}) {
                    if (instance.contains(key)) checker.fail(pointer + "/" + key, std::string("expected '") + key + "' only on an effect instance");
                }
                continue;
            }
            const json* effect = referenced[reference];
            if (!effect) continue;  // the effect's own problem is already reported
            const json& resources = set.value("resources", json::object());
            size_t textureCount = resources.contains("textures") ? resources["textures"].size() : 0;
            InputChecker inputs{[&](const std::string& p, const std::string& m) { checker.fail(p, m); }, textureCount};
            inputs.checkFields((*effect)["definition"]["inputs"], instance.value("inputs", json::object()), pointer + "/inputs", "");
            // A source repeated per list element has one style block per element.
            std::map<std::string, std::string> repeatedOver;
            for (const json& source : (*effect)["sources"]) repeatedOver[source["id"]] = source.value("for_each", "");
            const json& inputValues = instance.value("inputs", json::object());
            for (const auto& [id, style] : instance.value("style", json::object()).items()) {
                std::string at = pointer + "/style/" + id;
                auto source = repeatedOver.find(id);
                if (source == repeatedOver.end()) {
                    checker.fail(at, "expected style keyed by one of the effect's source ids, got '" + id + "'");
                    continue;
                }
                if (source->second.empty()) {
                    if (!style.is_object()) checker.fail(at, "expected one style block for source '" + id + "', got a list");
                    continue;
                }
                size_t elements = inputValues.contains(source->second) && inputValues[source->second].is_array()
                                      ? inputValues[source->second].size()
                                      : 0;
                if (!style.is_array() || style.size() != elements) {
                    checker.fail(at, "expected one style block per element of '" + source->second + "' (" +
                                         std::to_string(elements) + "), got " + describeValue(style));
                }
            }
            for (const auto& [path, _] : instance.value("provenance", json::object()).items()) {
                const json* at = &instance;
                std::stringstream steps(path);
                for (std::string step; at && std::getline(steps, step, '.');) {
                    if (at->is_array()) {
                        bool index = !step.empty() && std::all_of(step.begin(), step.end(), ::isdigit);
                        at = index && std::stoul(step) < at->size() ? &(*at)[std::stoul(step)] : nullptr;
                    } else {
                        at = at->is_object() && at->contains(step) ? &(*at)[step] : nullptr;
                    }
                }
                if (!at) checker.fail(pointer + "/provenance", "expected provenance for a value the instance holds, got '" + path + "'");
            }
            if ((*effect)["definition"].value("status", "") == "placeholder" && warnings) {
                std::string name = (*effect)["subject"]["name"];
                warnings->push_back({checker.file.string() + "#" + checker.prefix + pointer,
                                     "instance of placeholder effect '" + name + "': classify it before a release build"});
            }
        }
    }
};

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

std::vector<Problem> validateDocument(const fs::path& path, std::vector<fs::path>* checked,
                                      std::vector<Problem>* warnings) {
    std::vector<Problem> problems;
    std::vector<fs::path> seen;
    fs::path file = fs::is_directory(path) ? path / "manifest.json" : path;
    Validation validation{problems, warnings, seen, {}};
    validation.file(file);
    if (checked) *checked = seen;
    return problems;
}

}  // namespace husk::bundle_schema
