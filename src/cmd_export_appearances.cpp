#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include "cli_args.hpp"
#include "commands.hpp"
#include "corpus_store.hpp"
#include "json_writer.hpp"
#include "version.hpp"
#include "writers/bundle_common.hpp"

namespace fs = std::filesystem;
// Ordered: entries and subjects are re-emitted in the order husk wrote them.
using Json = nlohmann::ordered_json;

namespace husk::commands {

namespace {

const char* kListName = "appearances.json";

std::string readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("expected a readable " + path.string() + ", got none");
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

Json parseJson(const std::string& text, const fs::path& path) {
    Json doc = Json::parse(text, nullptr, false);
    if (doc.is_discarded()) throw std::runtime_error("expected JSON in " + path.string() + ", got unparseable text");
    return doc;
}

// Byte spans in JSON text that already parsed: the manifest is rewritten by
// replacing one reference's text, so nothing else in it is re-encoded (a
// reparsed number can't always be written back as the same text: -0).
size_t skipSpace(const std::string& t, size_t p) {
    while (p < t.size() && (t[p] == ' ' || t[p] == '\n' || t[p] == '\r' || t[p] == '\t')) ++p;
    return p;
}

size_t stringEnd(const std::string& t, size_t p) {
    for (++p; t[p] != '"'; ++p) {
        if (t[p] == '\\') ++p;
    }
    return p + 1;
}

size_t valueEnd(const std::string& t, size_t p) {
    if (t[p] == '"') return stringEnd(t, p);
    if (t[p] != '{' && t[p] != '[') return t.find_first_of(",}] \n\r\t", p);
    int depth = 0;
    while (true) {
        char c = t[p];
        if (c == '"') {
            p = stringEnd(t, p);
            continue;
        }
        if (c == '{' || c == '[') ++depth;
        if (c == '}' || c == ']') --depth;
        ++p;
        if (depth == 0) return p;
    }
}

// Start of the object member `key`'s value; `object` is at its '{'.
size_t memberStart(const std::string& t, size_t object, const std::string& key) {
    size_t p = skipSpace(t, object + 1);
    while (t[p] != '}') {
        size_t keyEnd = stringEnd(t, p);
        bool match = t.compare(p + 1, keyEnd - p - 2, key) == 0;
        p = skipSpace(t, skipSpace(t, keyEnd) + 1);
        if (match) return p;
        p = skipSpace(t, valueEnd(t, p));
        if (t[p] == ',') p = skipSpace(t, p + 1);
    }
    throw std::logic_error("export-appearances: a parsed object lost its member '" + key + "'");
}

size_t elementStart(const std::string& t, size_t array, size_t index) {
    size_t p = skipSpace(t, array + 1);
    for (size_t i = 0; i < index; ++i) p = skipSpace(t, skipSpace(t, valueEnd(t, p)) + 1);
    return p;
}

// husk's writer output for a parsed value; integers stay integers, and a
// float reads back as the shortest text husk wrote for it.
void emit(json::Writer& w, const Json& v) {
    switch (v.type()) {
        case Json::value_t::object:
            w.beginObject();
            for (const auto& [key, member] : v.items()) {
                w.key(key);
                emit(w, member);
            }
            w.endObject();
            return;
        case Json::value_t::array:
            w.beginArray();
            for (const Json& element : v) emit(w, element);
            w.endArray();
            return;
        case Json::value_t::string: w.value(v.get<std::string>()); return;
        case Json::value_t::boolean: w.value(v.get<bool>()); return;
        case Json::value_t::number_integer: w.value(v.get<int64_t>()); return;
        case Json::value_t::number_unsigned: w.value(static_cast<int64_t>(v.get<uint64_t>())); return;
        case Json::value_t::number_float: w.value(v.get<double>()); return;
        default: w.nullValue(); return;
    }
}

// IN, critical: a tree manifest's subject. Tree models are named by file
// id (export-world's model pass), which is what makes their reference ids
// the same as every other document's.
std::string modelReferenceId(const Json& subject, const fs::path& manifest) {
    const Json& id = subject["id"];
    if (id.value("kind", "") != "file_data_id") {
        throw std::runtime_error(manifest.string() + ": expected a subject identified by file_data_id, got " + id.dump());
    }
    canon::Ref ref{canon::FileDataId{id["value"].get<uint32_t>()}, "", canon::NameSource::None};
    return writers::referenceId("model", ref);
}

// A shared-list entry: id, then the model it belongs to, then the body.
Json sharedEntry(const Json& entry, const std::string& modelId) {
    Json out = {{"id", entry["id"]}, {"model", modelId}};
    for (const auto& [key, value] : entry.items()) {
        if (key != "id" && key != "model") out[key] = value;
    }
    return out;
}

// The manifest with its appearances reference (element `index` of
// references) in the external form, written by the model writer's own
// code at the reference's depth (root object, references array).
std::string externalManifest(const std::string& text, size_t index, const std::string& uri) {
    std::ostringstream rendered;
    json::Writer w(rendered);
    writers::beginListReference(w, "appearances", uri);
    w.endObject();
    std::string reference;
    for (char c : rendered.str()) {
        reference += c;
        if (c == '\n') reference += "    ";
    }
    size_t start = elementStart(text, memberStart(text, skipSpace(text, 0), "references"), index);
    size_t end = valueEnd(text, start);
    return text.substr(0, start) + reference + text.substr(end);
}

struct ModelList {
    Json reference;              // the list's own model reference
    std::vector<Json> entries;   // shared-list form, `model` set
};

}  // namespace

void addExportAppearancesOptions(CLI::App& app, ExportAppearancesOptions& opts) {
    app.add_option("tree", opts.tree,
                   "an export tree (export-world or export-corpus output): appearances.json is written there and "
                   "its model manifests are pointed at it")
        ->required();
}

int exportAppearances(int argc, char** args) {
    CLI::App app{"husk export-appearances -- move an export tree's appearance lists into one shared appearances.json",
                 "husk export-appearances"};
    ExportAppearancesOptions opts;
    addExportAppearancesOptions(app, opts);
    if (auto exit = parseCommandArgs(app, argc, args)) return *exit;
    try {
        fs::path tree = opts.tree;
        corpus::stampProducer(tree, std::string("husk ") + husk::version());
        fs::path listPath = tree / kListName;
        // A rerun finds manifests already external: their entries come from
        // the list this command wrote before.
        std::map<std::string, std::vector<Json>> previous;
        if (fs::exists(listPath)) {
            Json old = parseJson(readText(listPath), listPath);
            for (const Json& entry : old["appearances"]) previous[entry["model"].get<std::string>()].push_back(entry);
        }

        std::vector<fs::path> manifests;
        if (fs::is_directory(tree / "models")) {
            for (const fs::directory_entry& bundle : fs::directory_iterator(tree / "models")) {
                if (fs::exists(bundle.path() / "manifest.json")) manifests.push_back(bundle.path() / "manifest.json");
            }
        }
        std::sort(manifests.begin(), manifests.end());

        std::map<std::string, ModelList> lists;  // by model reference id
        size_t moved = 0;
        size_t kept = 0;
        size_t presets = 0;
        for (const fs::path& path : manifests) {
            std::string text = readText(path);
            Json manifest = parseJson(text, path);
            Json& refs = manifest["references"];
            auto list = std::find_if(refs.begin(), refs.end(), [](const Json& r) { return r["kind"] == "appearances"; });
            if (list == refs.end()) continue;
            presets += manifest["selection"].value("presets", Json::array()).size();
            std::string modelId = modelReferenceId(manifest["subject"], path);
            ModelList& out = lists[modelId];
            out.reference = {{"id", modelId}, {"kind", "model"}, {"asset", manifest["subject"]}};
            out.reference["asset"]["uri"] = fs::relative(path, tree).generic_string();

            if (!list->contains("document")) {
                auto found = previous.find(modelId);
                if (found == previous.end()) {
                    throw std::runtime_error(path.string() + ": expected its appearances in " + listPath.string() +
                                             " (the manifest points there), got none for model " + modelId);
                }
                out.entries = found->second;
                kept += out.entries.size();
                continue;
            }
            for (const Json& entry : (*list)["document"]["appearances"]) out.entries.push_back(sharedEntry(entry, modelId));
            moved += out.entries.size();

            std::string uri = fs::relative(listPath, path.parent_path()).generic_string();
            std::string rewritten = externalManifest(text, static_cast<size_t>(list - refs.begin()), uri);
            list->erase("document");
            (*list)["asset"]["uri"] = uri;
            // The splice must be exactly the reference change; compared as
            // unordered DOMs, so a key-order slip still reads as equal and
            // only a content change fails.
            if (nlohmann::json::parse(rewritten) != nlohmann::json::parse(manifest.dump())) {
                throw std::logic_error(path.string() + ": the rewritten manifest differs from the original beyond its appearances reference");
            }
            // Replaced, never written through: a --link-store tree's files
            // are hardlinks into the store.
            corpus::writeTextAtomic(path, rewritten);
        }

        writers::Provenance provenance{"husk-bundle", {}};
        provenance.sources.push_back({"model manifests", "each model's appearances, from its inline list", true,
                                      std::to_string(lists.size()) + " models"});
        std::ostringstream text;
        husk::json::Writer w(text);
        w.beginObject();
        writers::writeContractHeader(w, "appearances", canon::Ref{canon::None{}, "appearances", canon::NameSource::Synthesized},
                                     provenance);
        w.key("definition");
        w.beginObject();
        w.endObject();
        w.key("selection");
        w.beginObject();
        w.endObject();
        w.key("references");
        w.beginArray();
        for (const auto& [id, model] : lists) emit(w, model.reference);
        w.endArray();
        w.key("appearances");
        w.beginArray();
        size_t total = 0;
        for (auto& [id, model] : lists) {
            std::sort(model.entries.begin(), model.entries.end(),
                      [](const Json& a, const Json& b) { return a["id"].get<std::string>() < b["id"].get<std::string>(); });
            for (const Json& entry : model.entries) emit(w, entry);
            total += model.entries.size();
        }
        w.endArray();
        w.endObject();
        corpus::writeTextAtomic(listPath, text.str());

        std::cout << kListName << ": " << total << " appearances of " << lists.size() << " models (" << moved
                  << " moved from inline lists, " << kept << " kept), " << presets << " presets point at them"
                  << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "husk: export-appearances: " << e.what() << "\n";
        return 1;
    }
}

}  // namespace husk::commands
