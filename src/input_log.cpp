#include "input_log.hpp"

#include <set>
#include <stdexcept>

#include "corpus_store.hpp"

namespace fs = std::filesystem;

namespace husk::inputs {

namespace {

thread_local Scope* currentScope = nullptr;
Log* runLog = nullptr;

}  // namespace

Log::Log(const fs::path& file, fs::path sourceRoot)
    : sourceRoot_(fs::absolute(std::move(sourceRoot)).lexically_normal()), out_(file, std::ios::app) {
    if (!out_) throw std::runtime_error("cannot open " + file.string());
}

void Log::add(const std::string& output, const fs::path& input) {
    fs::path absolute = fs::absolute(input).lexically_normal();
    fs::path relative = absolute.lexically_relative(sourceRoot_);
    std::string name = !relative.empty() && *relative.begin() != ".." ? relative.generic_string() : absolute.string();
    std::string md5;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (output == kRun && !runSeen_.insert(name).second) return;
        auto it = md5ByInput_.find(name);
        if (it != md5ByInput_.end()) md5 = it->second;
    }
    // Hashed outside the lock: a large input must not stall every thread.
    if (md5.empty()) md5 = corpus::hex(corpus::md5File(absolute));
    std::lock_guard<std::mutex> lock(mutex_);
    md5ByInput_.emplace(name, md5);
    out_ << output << '\t' << name << '\t' << md5 << '\n';
    out_.flush();
}

Scope::Scope(Log* log, std::string output) : log_(log), output_(std::move(output)), previous_(currentScope) {
    currentScope = this;
}

Scope::~Scope() { currentScope = previous_; }

Scope* Scope::current() { return currentScope; }

void Scope::use(const fs::path& input) {
    if (!log_) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!seen_.insert(input.string()).second) return;
    }
    log_->add(output_, input);
}

Scope::Adopt::Adopt(Scope* scope) : previous_(currentScope) { currentScope = scope; }

Scope::Adopt::~Adopt() { currentScope = previous_; }

void setRunLog(Log* log) { runLog = log; }

bool openUsed(std::ifstream& f, const fs::path& path) {
    f.open(path, std::ios::binary);
    if (f.is_open()) use(path);
    return f.is_open();
}

void use(const fs::path& input) {
    if (currentScope) {
        currentScope->use(input);
        return;
    }
    if (runLog) runLog->add(kRun, input);
}

}  // namespace husk::inputs
