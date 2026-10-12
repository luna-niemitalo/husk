#include "corpus_index.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include <nlohmann/json.hpp>

#include "corpus_store.hpp"
#include "parallel.hpp"
#include "progress.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

namespace husk::corpus {

namespace {

// Only selection and definition: a model's resources (a character's rig
// is ~100 MB of JSON) would cost memory on every worker for nothing.
json readIndexedParts(const fs::path& manifest) {
    std::ifstream in(manifest, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + manifest.string());
    json::parser_callback_t keep = [](int depth, json::parse_event_t event, json& parsed) {
        if (depth != 1 || event != json::parse_event_t::key) return true;
        const std::string& key = parsed.get_ref<const std::string&>();
        return key == "selection" || key == "definition";
    };
    return json::parse(in, keep);
}

std::optional<uint32_t> db2Row(const json& ref, const std::string& table) {
    const json* id = ref.contains("id") ? &ref["id"] : nullptr;
    if (!id || id->value("kind", "") != "db2_row" || id->value("table", "") != table) return std::nullopt;
    return (*id)["row"].get<uint32_t>();
}

struct Rows {
    std::set<std::tuple<uint32_t, std::string, uint32_t>> displays;  // display, model, preset
    std::set<std::pair<uint32_t, std::string>> chrModels;             // chr_model, model
};

void indexManifest(const json& doc, const std::string& model, Rows& rows) {
    if (doc.contains("selection") && doc["selection"].contains("presets")) {
        const json& presets = doc["selection"]["presets"];
        for (uint32_t i = 0; i < presets.size(); i++) {
            if (!presets[i].contains("sources")) continue;
            for (const json& source : presets[i]["sources"]) {
                if (auto display = db2Row(source["ref"], "CreatureDisplayInfo")) rows.displays.insert({*display, model, i});
            }
        }
    }
    if (doc.contains("definition") && doc["definition"].contains("customization")) {
        for (const json& option : doc["definition"]["customization"]["options"]) {
            if (auto chrModel = db2Row(option["chr_model"], "ChrModel")) rows.chrModels.insert({*chrModel, model});
        }
    }
}

}  // namespace

IndexResult buildIndex(const fs::path& tree) {
    std::vector<fs::path> manifests;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(tree / "models", ec)) {
        fs::path manifest = entry.path() / "manifest.json";
        if (fs::exists(manifest)) manifests.push_back(manifest);
    }
    std::sort(manifests.begin(), manifests.end());

    Rows rows;
    std::mutex rowsMutex;
    Progress progress("index", manifests.size());
    parallelFor(manifests.size(), 0, [&](size_t i) {
        Rows local;
        std::string model = manifests[i].lexically_relative(tree).generic_string();
        // One re-read before failing: this machine has returned wrong bytes
        // for a valid manifest once (2026-10-12), and a second read tells a
        // bad read from a bad file.
        try {
            indexManifest(readIndexedParts(manifests[i]), model, local);
        } catch (const std::exception& first) {
            std::cerr << "husk: warning: " << manifests[i].string() << ": " << first.what() << "; reading it again\n";
            local = {};
            try {
                indexManifest(readIndexedParts(manifests[i]), model, local);
            } catch (const std::exception& e) {
                throw std::runtime_error(manifests[i].string() + ": " + e.what());
            }
        }
        {
            std::lock_guard<std::mutex> lock(rowsMutex);
            rows.displays.merge(local.displays);
            rows.chrModels.merge(local.chrModels);
        }
        progress.tick();
    });

    std::ostringstream displays;
    displays << "display\tmodel\tpreset\n";
    for (const auto& [display, model, preset] : rows.displays) displays << display << '\t' << model << '\t' << preset << '\n';
    std::ostringstream chrModels;
    chrModels << "chr_model\tmodel\n";
    for (const auto& [chrModel, model] : rows.chrModels) chrModels << chrModel << '\t' << model << '\n';

    fs::create_directories(tree / "index");
    writeTextAtomic(tree / "index" / "creature_displays.tsv", displays.str());
    writeTextAtomic(tree / "index" / "chr_models.tsv", chrModels.str());
    return {manifests.size(), rows.displays.size(), rows.chrModels.size()};
}

}  // namespace husk::corpus
