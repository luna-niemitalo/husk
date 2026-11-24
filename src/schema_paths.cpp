// Every document kind's schema as the property paths a document can hold,
// with $refs followed: what `husk schema-doc` renders and what the x-source
// coverage check walks (bundle_schema.hpp).

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
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

// ---------------------------------------------------------------- rendering

std::string cell(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '|') out += "\\|";
        else if (c == '\n') out += ' ';
        else out += c;
    }
    return out;
}

std::string code(const std::string& text) { return "`" + text + "`"; }

std::string scalar(const json& value) { return value.is_string() ? value.get<std::string>() : value.dump(); }

std::string typeOf(const PathEntry& e) {
    const json& n = *e.resolved;
    std::string type;
    if (n.contains("type")) {
        if (n["type"].is_array()) {
            for (const json& t : n["type"]) type += (type.empty() ? "" : " or ") + t.get<std::string>();
        } else {
            type = n["type"];
        }
    } else if (n.contains("const")) {
        type = "constant";
    } else if (n.contains("enum")) {
        type = "enum";
    } else if (n.contains("oneOf")) {
        type = "one of " + std::to_string(n["oneOf"].size());
    }
    if (type == "array" && n.contains("items") && n["items"].is_object()) {
        const json& items = n["items"];
        if (items.contains("$ref")) type += " of " + code(refName(items["$ref"]));
        else if (items.contains("type") && items["type"].is_string()) type += " of " + items["type"].get<std::string>();
    }
    if (type == "array" && n.contains("items") && n["items"].is_array()) type = "tuple of " + std::to_string(n["items"].size());
    if (type == "object" && n.contains("additionalProperties") && n["additionalProperties"].is_object()) type = "map";
    if (!e.refName.empty()) type = code(e.refName) + (type.empty() ? "" : " (" + type + ")");
    if (e.recursive) type += ", recursive";
    return type;
}

std::string allowedOf(const json& n) {
    std::vector<std::string> parts;
    if (n.contains("const")) parts.push_back("= " + code(scalar(n["const"])));
    if (n.contains("enum")) {
        std::string values;
        for (const json& v : n["enum"]) values += (values.empty() ? "" : ", ") + code(scalar(v));
        parts.push_back("one of " + values);
    }
    static const std::vector<std::pair<const char*, const char*>> bounds = {
        {"minimum", "≥ "}, {"maximum", "≤ "}, {"exclusiveMinimum", "> "}, {"exclusiveMaximum", "< "},
        {"minLength", "length ≥ "}, {"maxLength", "length ≤ "}, {"minItems", "entries ≥ "}, {"maxItems", "entries ≤ "},
        {"minProperties", "keys ≥ "}, {"multipleOf", "multiple of "}};
    for (const auto& [key, text] : bounds) {
        if (n.contains(key)) parts.push_back(text + n[key].dump());
    }
    if (n.contains("pattern")) parts.push_back("matches " + code(n["pattern"].get<std::string>()));
    if (n.contains("uniqueItems") && n["uniqueItems"] == true) parts.push_back("entries unique");
    std::string out;
    for (const std::string& p : parts) out += (out.empty() ? "" : "; ") + p;
    return out;
}

// x-index-into/x-id-in may sit on the use site or on the definition.
std::string linksOf(const PathEntry& e) {
    std::string out;
    for (const json* n : {e.resolved, e.node}) {
        if (n->contains("x-index-into")) out = "index into " + code((*n)["x-index-into"]);
        if (n->contains("x-id-in")) out = "id in " + code((*n)["x-id-in"]);
    }
    return out;
}

std::string unitOf(const PathEntry& e) {
    if (e.node->contains("x-unit")) return (*e.node)["x-unit"];
    if (e.resolved->contains("x-unit")) return (*e.resolved)["x-unit"];
    return {};
}

std::string origin(const json& o) {
    if (o.contains("table")) return "table " + code(o["table"].get<std::string>() + (o.contains("column") ? "." + o["column"].get<std::string>() : ""));
    if (o.contains("file")) return "file " + code(o["file"]) + (o.contains("field") ? " " + code(o["field"]) : "");
    if (o.contains("derived")) return "derived by " + code(o["derived"]);
    if (o.contains("authored")) return "authored in " + code(o["authored"]);
    if (o.contains("stated_by")) return "stated per value by " + code(o["stated_by"]);
    if (o.contains("listfile")) return "listfile";
    return "format";
}

std::string sourceOf(const PathEntry& e) {
    if (!e.source) return e.resolved->contains("const") ? "format (constant)" : "—";
    std::string out;
    for (const json& o : *e.source) out += (out.empty() ? "" : "; ") + origin(o);
    if (!e.sourceFrom.empty()) out += " (from " + code(e.sourceFrom) + ")";
    return out;
}

// The first sentence says what the value is, the rest why
// (BUNDLE_FORMAT.md "Descriptions: what, then why"). A sentence ends at
// ". " before a capital or a backtick, so "e.g. a" and "M2Particle.gravity"
// don't split.
std::string descriptionOf(const PathEntry& e) {
    std::string text = e.node->value("description", e.resolved->value("description", ""));
    for (size_t at = text.find(". "); at != std::string::npos; at = text.find(". ", at + 1)) {
        char next = at + 2 < text.size() ? text[at + 2] : '\0';
        if (std::isupper(static_cast<unsigned char>(next)) || next == '`') {
            return text.substr(0, at + 1) + "<br>*Why:* " + text.substr(at + 2);
        }
    }
    return text;
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

std::string schemaDoc() {
    std::ostringstream md;
    std::string version = schemaFor("model")["properties"]["schema_version"]["const"];
    md << "# husk bundle format reference, schema " << version << "\n\n"
       << "Generated by `husk schema-doc` from the schemas embedded in husk (schema/*.schema.json); "
          "regenerate it rather than editing it. REFACTOR/BUNDLE_FORMAT.md explains the format; "
          "this file lists every value a document can hold.\n\n"
       << "- **Path**: `a.b` is property `b` of object `a`; `a[]` each entry of array `a`; "
          "`a.<name>` each value of map `a`; `a{kind=x}` the variant of `a` whose `kind` is `x` "
          "(`a{#i}` the i-th variant when no constant tells them apart).\n"
       << "- **Req**: `yes` when the enclosing object must have it.\n"
       << "- **Type**: a named shared or kind-local definition is in backticks.\n"
       << "- **Allowed**: constants, enums, bounds and patterns the schema enforces.\n"
       << "- **Unit**: `x-unit`, for every real-valued property.\n"
       << "- **Links**: `index into` an array (`x-index-into`: a bare name is a property of an enclosing object, "
          "`/...` a pointer into the document), or `id in` an array whose entries carry ids (`x-id-in`).\n"
       << "- **Source**: where the value comes from (`x-source`); \"from `p`\" when inherited from path `p`; "
          "— when not annotated yet.\n"
       << "- **Description**: what the value is (the first sentence), then *Why:* the reason it is so.\n\n";
    for (const std::string& kind : kinds()) {
        const json& schema = schemaFor(kind);
        md << "## " << kind << "\n\n"
           << "`$schema`: `" << schema["$id"].get<std::string>() << "`\n\n"
           << schema.value("description", "") << "\n\n"
           << "| Path | Req | Type | Allowed | Unit | Links | Source | Description |\n"
           << "|---|---|---|---|---|---|---|---|\n";
        for (const PathEntry& e : documentPaths(kind)) {
            md << "| " << cell(code(e.path)) << " | " << (e.required ? "yes" : "") << " | " << cell(typeOf(e)) << " | "
               << cell(allowedOf(*e.resolved)) << " | " << cell(unitOf(e)) << " | " << cell(linksOf(e)) << " | "
               << cell(sourceOf(e)) << " | " << cell(descriptionOf(e)) << " |\n";
        }
        md << "\n";
    }
    return md.str();
}

}  // namespace husk::bundle_schema
