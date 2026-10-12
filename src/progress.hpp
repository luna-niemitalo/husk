#pragma once

#include <atomic>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>

namespace husk {

// "<phase>: done/total" on stdout every 500 items and at the end, flushed,
// so a log of a run that dies shows how far it got.
class Progress {
public:
    Progress(std::string phase, size_t total) : phase_(std::move(phase)), total_(total) {}
    void tick() {
        size_t done = ++done_;
        if (done % 500 == 0 || done == total_) {
            std::lock_guard<std::mutex> lock(mutex_);
            std::cout << phase_ << ": " << done << "/" << total_ << std::endl;
        }
    }

private:
    std::string phase_;
    size_t total_;
    std::atomic<size_t> done_{0};
    std::mutex mutex_;
};

}  // namespace husk
