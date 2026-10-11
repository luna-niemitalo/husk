#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>

// Which source files each output of an export was made from: the corpus
// store's inputs.tsv (REFACTOR/CORPUS_EXPORT.md "Incremental export").
//
// Recorded where a source file is used (read, or taken from a cache filled
// by an earlier read), not where it is opened, so an output that reuses a
// cached table still names it. A file used outside any output's scope (the
// DB2 tables and listfile a run loads up front) is the run's: every output
// depends on it.
namespace husk::inputs {

// inputs.log beside an export's errors.log: one
// "output<TAB>input<TAB>md5" line per use, appended by every resumed run.
// Inputs under `sourceRoot` are written relative to it.
class Log {
public:
    Log(const std::filesystem::path& file, std::filesystem::path sourceRoot);
    void add(const std::string& output, const std::filesystem::path& input);

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
    friend void use(const std::filesystem::path& input);
    void use(const std::filesystem::path& input);
    Log* log_;
    std::string output_;
    Scope* previous_;
    std::mutex mutex_;  // adopted by worker threads
    std::set<std::string> seen_;
};

// The run's log, for uses outside any scope; null when nothing records.
void setRunLog(Log* log);

// Records that the current output used `input`.
void use(const std::filesystem::path& input);

}  // namespace husk::inputs
