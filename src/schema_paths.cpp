// Every document kind's schema as the property paths a document can hold,
// with $refs followed: what the x-source coverage check walks
// (bundle_schema.hpp).

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "bundle_schema.hpp"
#include "bundle_schema_internal.hpp"

namespace husk::bundle_schema {

namespace {

using namespace internal;

struct PathEntry {
    std::string path;       // "sources[].region{kind=sphere}.inner_radius"
    std::string where;      // "<stem>.schema.json#<pointer>" of the property's own schema
    const json* node;       // the property's schema at its use site
    const json* resolved;   // the same, $refs followed
    std::string refName;    // the definition a $ref names, empty without one
    bool required = false;
    bool leaf = false;
    bool recursive = false;  // a $ref to a definition already being expanded on this path
    const json* source = nullptr;  // the x-source in effect, own or inherited
    std::string sourceFrom;        // the path it is inherited from; empty when the property's own
};

struct Located {
    std::string stem;
    std::string pointer;
    const json* node;
};

std::string refName(const std::string& ref) { return ref.substr(ref.find_last_of('/') + 1); }

// A $ref chain's end, and the first x-source met on the way: a property's
// own x-source wins over its definition's.
Located follow(Located at, const json*& source, std::string& name) {
    if (at.node->contains("x-source")) source = &(*at.node)["x-source"];
    for (int hops = 0; at.node->contains("$ref") && hops < 8; ++hops) {
        std::string ref = (*at.node)["$ref"];
        if (name.empty()) name = refName(ref);
        size_t hash = ref.find('#');
        if (hash != 0) at.stem = schemaStem(ref.substr(0, hash));
        at.pointer = ref.substr(hash + 1);
        at.node = &schemaFor(at.stem).at(json::json_pointer(at.pointer));
        if (!source && at.node->contains("x-source")) source = &(*at.node)["x-source"];
    }
    return at;
}

std::string escapeKey(const std::string& key) {
    std::string out;
    for (char c : key) {
        if (c == '~') out += "~0";
        else if (c == '/') out += "~1";
        else out += c;
    }
    return out;
}

// A oneOf branch's name in a path: the constant that tells it apart.
std::string branchLabel(const json& branch, size_t index) {
    for (const auto& [key, sub] : branch.value("properties", json::object()).items()) {
        if (sub.contains("const")) return "{" + key + "=" + (sub["const"].is_string() ? sub["const"].get<std::string>() : sub["const"].dump()) + "}";
    }
    return "{#" + std::to_string(index) + "}";
}

struct Expander {
    std::vector<PathEntry>& out;
    std::vector<std::string> expanding;  // "<stem>#<pointer>" of the definitions on the current path

    bool enter(const std::string& name, const std::string& key) {
        if (name.empty()) return true;
        if (std::find(expanding.begin(), expanding.end(), key) != expanding.end()) return false;
        expanding.push_back(key);
        return true;
    }

    void leave(const std::string& name) {
        if (!name.empty()) expanding.pop_back();
    }

    void property(const std::string& path, const Located& at, bool required, const json* inherited, const std::string& inheritedFrom) {
        const json* own = nullptr;
        std::string name;
        Located end = follow(at, own, name);
        size_t index = out.size();
        PathEntry entry{path, at.stem + kSchemaSuffix + "#" + at.pointer, at.node, end.node, name, required};
        entry.source = own ? own : inherited;
        entry.sourceFrom = own ? "" : inheritedFrom;
        out.push_back(entry);
        if (!enter(name, end.stem + "#" + end.pointer)) {
            out[index].recursive = true;
            out[index].leaf = true;
            return;
        }
        children(path, end, out[index].source, own ? path : inheritedFrom);
        leave(name);
        out[index].leaf = out.size() == index + 1;
    }

    // The rows below a resolved node: its properties, map values, array
    // entries and oneOf branches, which are paths but not rows of their own.
    void children(const std::string& path, const Located& at, const json* source, const std::string& from) {
        const json& node = *at.node;
        auto join = [&](const std::string& step) { return path.empty() ? step : path + "." + step; };
        if (node.contains("properties")) {
            std::vector<std::string> required = node.value("required", std::vector<std::string>{});
            for (const auto& [key, sub] : node["properties"].items()) {
                bool isRequired = std::find(required.begin(), required.end(), key) != required.end();
                property(join(key), {at.stem, at.pointer + "/properties/" + escapeKey(key), &sub}, isRequired, source, from);
            }
        }
        if (node.contains("additionalProperties") && node["additionalProperties"].is_object()) {
            nested(join("<name>"), {at.stem, at.pointer + "/additionalProperties", &node["additionalProperties"]}, source, from);
        }
        if (node.contains("items") && node["items"].is_object()) {
            nested(path + "[]", {at.stem, at.pointer + "/items", &node["items"]}, source, from);
        }
        for (const char* combinator : {"oneOf", "anyOf", "allOf"}) {
            if (!node.contains(combinator)) continue;
            for (size_t i = 0; i < node[combinator].size(); ++i) {
                Located branch{at.stem, at.pointer + "/" + combinator + "/" + std::to_string(i), &node[combinator][i]};
                nested(path + branchLabel(*branch.node, i), branch, source, from);
            }
        }
    }

    // A map value's, array entry's or oneOf branch's schema: its x-source
    // applies to the rows below it.
    void nested(const std::string& path, const Located& at, const json* source, const std::string& from) {
        const json* own = nullptr;
        std::string name;
        Located end = follow(at, own, name);
        if (!enter(name, end.stem + "#" + end.pointer)) return;
        children(path, end, own ? own : source, own ? path : from);
        leave(name);
    }
};

std::vector<PathEntry> documentPaths(const std::string& kind) {
    std::vector<PathEntry> out;
    Expander expander{out, {}};
    expander.children("", {kind, "", &schemaFor(kind)}, nullptr, "");
    return out;
}

}  // namespace

std::vector<Problem> sourceGaps() {
    std::vector<Problem> out;
    std::set<std::string> seen;
    for (const std::string& kind : kinds()) {
        for (const PathEntry& e : documentPaths(kind)) {
            // A constant's origin is the format itself.
            if (!e.leaf || e.recursive || e.source || e.resolved->contains("const")) continue;
            if (!seen.insert(e.where).second) continue;
            out.push_back({e.where, "leaf property has no x-source, own or inherited (" + kind + " document path " + e.path + ")"});
        }
    }
    std::sort(out.begin(), out.end(), [](const Problem& a, const Problem& b) { return a.where < b.where; });
    return out;
}

}  // namespace husk::bundle_schema
