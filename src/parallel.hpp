#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

#include "input_log.hpp"

namespace husk {

// Runs work(i) for i in [0, n) on `jobs` threads (0: one per hardware
// thread). The first exception a work item lets escape stops handing out
// new items and is rethrown once every thread has finished. The workers
// record source uses for the caller's output (input_log.hpp).
template <typename Work>
void parallelFor(size_t n, unsigned jobs, Work work) {
    if (jobs == 0) jobs = std::max(1u, std::thread::hardware_concurrency());
    std::atomic<size_t> next{0};
    std::mutex failureMutex;
    std::exception_ptr failure;
    inputs::Scope* caller = inputs::Scope::current();
    {
        std::vector<std::jthread> threads;
        for (unsigned j = 0; j < jobs; ++j) {
            threads.emplace_back([&, caller] {
                inputs::Scope::Adopt adopt(caller);
                for (size_t i = next++; i < n; i = next++) {
                    try {
                        work(i);
                    } catch (...) {
                        std::lock_guard<std::mutex> lock(failureMutex);
                        if (!failure) failure = std::current_exception();
                        next = n;
                    }
                }
            });
        }
    }
    if (failure) std::rethrow_exception(failure);
}

}  // namespace husk
