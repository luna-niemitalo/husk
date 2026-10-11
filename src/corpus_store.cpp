#include "corpus_store.hpp"

#include <algorithm>
#include <atomic>
#include <exception>
#include <fstream>
#include <iterator>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <unistd.h>

#include "json_writer.hpp"
#include "md5.h"
#include "writers/bundle_common.hpp"

namespace fs = std::filesystem;

namespace husk::corpus {

namespace {

constexpr size_t kChunk = 1 << 20;
// The contract header (format .. provenance) is the first thing every
// document writes; this much of the file always holds it.
constexpr size_t kHeaderBytes = 8192;

std::atomic<uint64_t> stagingCounter{0};

Md5 finish(MD5_CTX& ctx) {
    Md5 out;
    MD5_Final(out.data(), &ctx);
    return out;
}

// Copies `source` to `staged`, hashing the bytes as they are written.
Md5 copyHashing(const fs::path& source, const fs::path& staged) {
    std::ifstream in(source, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + source.string());
    std::ofstream out(staged, std::ios::binary);
    if (!out) throw std::runtime_error("cannot create " + staged.string());
    MD5_CTX ctx;
    MD5_Init(&ctx);
    std::vector<char> buffer(kChunk);
    while (in.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || in.gcount() > 0) {
        MD5_Update(&ctx, buffer.data(), static_cast<unsigned long>(in.gcount()));
        out.write(buffer.data(), in.gcount());
    }
    out.close();
    if (!out) throw std::runtime_error("cannot write " + staged.string());
    return finish(ctx);
}

std::vector<std::string> subdirectories(const fs::path& dir) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec))
        if (entry.is_directory() && validLabel(entry.path().filename().string()))
            out.push_back(entry.path().filename().string());
    std::sort(out.begin(), out.end());
    return out;
}

// The string value of `key` in a document's header text; nullopt when the
// text is not a husk document or lacks the key.
std::optional<std::string> headerString(const std::string& head, const std::string& key) {
    size_t at = head.find("\"" + key + "\"");
    if (at == std::string::npos) return std::nullopt;
    size_t colon = head.find(':', at + key.size() + 2);
    if (colon == std::string::npos) return std::nullopt;
    size_t open = head.find('"', colon);
    if (open == std::string::npos) return std::nullopt;
    size_t close = head.find('"', open + 1);
    if (close == std::string::npos) return std::nullopt;
    return head.substr(open + 1, close - open - 1);
}

struct DocumentHeader {
    std::string producer;
    std::string schemaVersion;
};

std::optional<DocumentHeader> readHeader(const fs::path& path) {
    if (path.extension() != ".json") return std::nullopt;
    std::ifstream in(path, std::ios::binary);
    std::string head(kHeaderBytes, '\0');
    in.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(in.gcount()));
    if (headerString(head, "format") != "husk-bundle") return std::nullopt;
    auto producer = headerString(head, "producer");
    auto schema = headerString(head, "schema_version");
    if (!producer || !schema)
        throw std::runtime_error(path.string() + ": husk document without provenance.producer and schema_version");
    return DocumentHeader{*producer, *schema};
}

// errors.log rows ("phase<TAB>item<TAB>message", appended by every resumed
// run), deduplicated.
std::vector<MissingRow> readErrorLog(const fs::path& path) {
    std::set<std::tuple<std::string, std::string, std::string>> rows;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        size_t a = line.find('\t');
        size_t b = a == std::string::npos ? a : line.find('\t', a + 1);
        if (b == std::string::npos) {
            rows.insert({"", "", line});
            continue;
        }
        rows.insert({line.substr(0, a), line.substr(a + 1, b - a - 1), line.substr(b + 1)});
    }
    std::vector<MissingRow> out;
    for (const auto& [phase, item, reason] : rows) out.push_back({phase, item, reason});
    return out;
}

const std::string kErrorLog = "errors.log";

// The producer's version from "husk <version>": what labels the build.
std::string huskLabel(const std::string& producer) {
    const std::string prefix = "husk ";
    if (producer.rfind(prefix, 0) != 0)
        throw std::runtime_error("producer \"" + producer + "\" is not husk's (expected \"husk <version>\")");
    std::string version = producer.substr(prefix.size());
    if (version == "unknown" || version.find("dirty") != std::string::npos)
        throw std::runtime_error("producer \"" + producer +
                                 "\" names no commit: export from a clean, reconfigured build to store it");
    if (!validLabel(version)) throw std::runtime_error("producer version \"" + version + "\" is not a valid label");
    return version;
}

}  // namespace

std::string hex(const Md5& key) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (uint8_t b : key) {
        out += digits[b >> 4];
        out += digits[b & 15];
    }
    return out;
}

std::optional<Md5> parseHex(const std::string& text) {
    if (text.size() != 32) return std::nullopt;
    Md5 out{};
    for (size_t i = 0; i < 32; i++) {
        char c = text[i];
        int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (v < 0) return std::nullopt;
        out[i / 2] = static_cast<uint8_t>(out[i / 2] << 4 | v);
    }
    return out;
}

Md5 md5File(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path.string() + " to hash it");
    MD5_CTX ctx;
    MD5_Init(&ctx);
    std::vector<char> buffer(kChunk);
    while (in.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) || in.gcount() > 0)
        MD5_Update(&ctx, buffer.data(), static_cast<unsigned long>(in.gcount()));
    return finish(ctx);
}

bool validLabel(const std::string& label) {
    if (label.empty() || label == "." || label == "..") return false;
    return label.find('/') == std::string::npos && label.find('\0') == std::string::npos &&
           label.find('\n') == std::string::npos;
}

std::optional<BuildId> parseBuildPath(const std::string& text) {
    size_t slash = text.find('/');
    if (slash == std::string::npos) return std::nullopt;
    BuildId id{text.substr(0, slash), text.substr(slash + 1)};
    if (!validLabel(id.game) || !validLabel(id.husk)) return std::nullopt;
    return id;
}

void writeTextAtomic(const fs::path& path, const std::string& text) {
    fs::path staged = path.string() + ".partial";
    {
        std::ofstream out(staged, std::ios::binary);
        out << text;
        out.close();
        if (!out) throw std::runtime_error("cannot write " + staged.string());
    }
    fs::rename(staged, path);
}

fs::path Store::objectPath(const Md5& key) const {
    std::string h = hex(key);
    return root_ / "objects" / h.substr(0, 2) / h.substr(2, 2) / h;
}

Store::Put Store::putFile(const fs::path& source) const {
    Md5 key = md5File(source);
    fs::path target = objectPath(key);
    if (fs::exists(target)) return {key, false};

    fs::path staged = root_ / "tmp" / (std::to_string(::getpid()) + "-" + std::to_string(stagingCounter++));
    fs::create_directories(staged.parent_path());
    Md5 written = copyHashing(source, staged);
    if (written != key) {
        fs::remove(staged);
        throw std::runtime_error(source.string() + " changed while being stored: hashed " + hex(key) +
                                 ", copied " + hex(written));
    }
    fs::create_directories(target.parent_path());
    // Read-only: a reflinked or hardlinked view writing into an object would
    // change it for every build.
    fs::permissions(staged, fs::perms::owner_read | fs::perms::group_read | fs::perms::others_read);
    fs::rename(staged, target);
    return {key, true};
}

std::vector<std::string> Store::games() const {
    std::vector<std::string> out = subdirectories(root_ / "builds");
    std::erase_if(out, [&](const std::string& game) { return huskBuilds(game).empty(); });
    return out;
}

// Only complete builds: ingestTree writes files.tsv last.
std::vector<std::string> Store::huskBuilds(const std::string& game) const {
    std::vector<std::string> out = subdirectories(root_ / "builds" / game);
    std::erase_if(out, [&](const std::string& husk) {
        return !fs::exists(buildDir({game, husk}) / "files.tsv");
    });
    return out;
}

std::optional<BuildId> Store::newest(const std::string& game) const {
    std::optional<BuildId> best;
    std::string bestTime;
    for (const std::string& husk : huskBuilds(game)) {
        BuildId id{game, husk};
        std::ifstream in(buildDir(id) / "meta.json", std::ios::binary);
        std::string meta{std::istreambuf_iterator<char>(in), {}};
        std::string time = headerString(meta, "stored_at").value_or("");
        if (!best || time > bestTime) {
            best = id;
            bestTime = time;
        }
    }
    return best;
}

std::map<std::string, BuildId> Store::tags() const {
    std::map<std::string, BuildId> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(root_ / "tags", ec)) {
        std::string name = entry.path().filename().string();
        if (!validLabel(name) || name.front() == '.') continue;
        std::ifstream in(entry.path());
        std::string line;
        if (!std::getline(in, line)) continue;
        if (auto id = parseBuildPath(line)) out[name] = *id;
    }
    return out;
}

void Store::setTag(const std::string& name, const BuildId& id) const {
    if (!validLabel(name) || name.front() == '.') throw std::runtime_error("invalid tag name \"" + name + "\"");
    fs::create_directories(root_ / "tags");
    writeTextAtomic(root_ / "tags" / name, id.path() + "\n");
}

IngestResult ingestTree(const Store& store, const fs::path& tree, const std::string& game) {
    if (!validLabel(game)) throw std::runtime_error("invalid game build label \"" + game + "\"");
    if (!fs::is_directory(tree)) throw std::runtime_error(tree.string() + " is not a directory");

    std::vector<fs::path> files;
    for (const auto& entry : fs::recursive_directory_iterator(tree)) {
        if (!entry.is_regular_file()) continue;
        fs::path relative = entry.path().lexically_relative(tree);
        if (relative == kErrorLog) continue;
        // Interrupted writes of the exporters (TextureStore, writeTextAtomic).
        if (relative.extension() == ".partial") continue;
        files.push_back(relative);
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) throw std::runtime_error(tree.string() + " has no files to store");

    struct Row {
        Md5 key{};
        uint64_t size = 0;
        bool added = false;
        std::optional<DocumentHeader> header;
    };
    std::vector<Row> rows(files.size());
    std::atomic<size_t> next{0};
    std::mutex failureMutex;
    std::exception_ptr failure;
    auto work = [&] {
        for (size_t i = next++; i < files.size(); i = next++) {
            try {
                fs::path source = tree / files[i];
                Store::Put put = store.putFile(source);
                rows[i] = {put.key, fs::file_size(source), put.added, readHeader(source)};
            } catch (...) {
                std::lock_guard<std::mutex> lock(failureMutex);
                if (!failure) failure = std::current_exception();
                next = files.size();
            }
        }
    };
    {
        std::vector<std::jthread> workers;
        unsigned count = std::max(1u, std::thread::hardware_concurrency());
        for (unsigned i = 0; i < count; i++) workers.emplace_back(work);
    }
    if (failure) std::rethrow_exception(failure);

    std::optional<DocumentHeader> build;
    for (size_t i = 0; i < files.size(); i++) {
        if (!rows[i].header) continue;
        if (!build) {
            build = rows[i].header;
            continue;
        }
        if (rows[i].header->producer != build->producer || rows[i].header->schemaVersion != build->schemaVersion)
            throw std::runtime_error("documents disagree on who wrote them: " + files[i].string() + " says " +
                                     rows[i].header->producer + " / " + rows[i].header->schemaVersion +
                                     ", others " + build->producer + " / " + build->schemaVersion);
    }
    if (!build) throw std::runtime_error(tree.string() + " holds no husk documents");

    IngestResult result;
    result.id = {game, huskLabel(build->producer)};
    std::ostringstream filesTsv;
    filesTsv << "path\tmd5\n";
    for (size_t i = 0; i < files.size(); i++) {
        filesTsv << files[i].generic_string() << '\t' << hex(rows[i].key) << '\n';
        result.bytes += rows[i].size;
        result.addedObjects += rows[i].added ? 1 : 0;
    }
    result.files = files.size();

    fs::path dir = store.buildDir(result.id);
    fs::path filesPath = dir / "files.tsv";
    if (fs::exists(filesPath)) {
        std::ifstream in(filesPath, std::ios::binary);
        std::string existing{std::istreambuf_iterator<char>(in), {}};
        if (existing != filesTsv.str())
            throw std::runtime_error(dir.string() +
                                     " already holds this build with other files: the same husk revision "
                                     "exported this game build differently; re-export or remove that build");
        result.alreadyStored = true;
        return result;
    }

    std::vector<MissingRow> missing = readErrorLog(tree / kErrorLog);
    result.missing = missing.size();
    std::ostringstream missingTsv;
    missingTsv << "phase\titem\treason\n";
    for (const MissingRow& row : missing) missingTsv << row.phase << '\t' << row.item << '\t' << row.reason << '\n';

    std::ostringstream meta;
    json::Writer w(meta);
    w.beginObject();
    w.key("game");
    w.value(result.id.game);
    w.key("husk");
    w.value(result.id.husk);
    w.key("producer");
    w.value(build->producer);
    w.key("schema_version");
    w.value(build->schemaVersion);
    w.key("files");
    w.value(static_cast<int64_t>(result.files));
    w.key("bytes");
    w.value(static_cast<int64_t>(result.bytes));
    w.key("new_objects");
    w.value(static_cast<int64_t>(result.addedObjects));
    w.key("missing");
    w.value(static_cast<int64_t>(result.missing));
    w.key("source_tree");
    w.value(fs::absolute(tree).lexically_normal().string());
    w.key("stored_at");
    w.value(writers::isoTimestampUtc());
    w.endObject();
    meta << "\n";

    fs::create_directories(dir);
    writeTextAtomic(dir / "missing.tsv", missingTsv.str());
    writeTextAtomic(dir / "meta.json", meta.str());
    // Last: a build is listed as complete only once files.tsv exists.
    writeTextAtomic(filesPath, filesTsv.str());
    return result;
}

}  // namespace husk::corpus
