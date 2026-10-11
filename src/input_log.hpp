#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

// Which source files each output of an export used, for the corpus
// store's inputs.tsv (REFACTOR/CORPUS_EXPORT.md "Input tracking").
namespace husk::inputs {

// inputs.log beside an export's errors.log: one
// "output<TAB>input<TAB>md5" line per use, appended by every resumed run.
// Inputs under `sourceRoot` are written relative to it.
class Log {
public:
    Log(const std::filesystem::path& file, std::filesystem::path sourceRoot);
    // `bytes`: the input's contents when the caller has read them, so it
    // isn't read again to be hashed; null when it hasn't.
    void add(const std::string& output, const std::filesystem::path& input, std::span<const uint8_t> const* bytes);

private:
    std::filesystem::path sourceRoot_;
    std::mutex mutex_;
    std::ofstream out_;
    std::unordered_map<std::string, std::string> md5ByInput_;  // each input hashed once per run
    // The run's uses are few (tables, the listfile) but repeat on every
    // cache hit outside a scope.
    std::set<std::string> runSeen_;
};

// The output name for inputs used outside any scope.
inline const std::string kRun = "*";

// While alive, uses on this thread are attributed to `output`. A null log
// discards them: discovery passes read sources whose results only choose
// what to export.
class Scope {
public:
    Scope(Log* log, std::string output);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

    // The scope a worker thread should carry on for its spawner.
    static Scope* current();
    // Carries `scope` on this thread for the life of the object.
    class Adopt {
    public:
        explicit Adopt(Scope* scope);
        ~Adopt();

    private:
        Scope* previous_;
    };

private:
    friend void use(const std::filesystem::path& input, std::span<const uint8_t> const* bytes);
    void use(const std::filesystem::path& input, std::span<const uint8_t> const* bytes);
    Log* log_;
    std::string output_;
    Scope* previous_;
    std::mutex mutex_;  // adopted by worker threads
    std::set<std::string> seen_;
};

// The run's log, for uses outside any scope; null when nothing records.
void setRunLog(Log* log);

// Records that the current output used `input`: a file it read (pass its
// bytes) or took from a cache an earlier read filled.
void use(const std::filesystem::path& input, std::span<const uint8_t> const* bytes);
void use(const std::filesystem::path& input, std::span<const uint8_t> bytes);
void use(const std::filesystem::path& input);

// Reads `path` whole and records it, or nullopt if it can't be opened: a
// reader trying several candidates records the one it read.
std::optional<std::vector<uint8_t>> readUsed(const std::filesystem::path& path);

}  // namespace husk::inputs
