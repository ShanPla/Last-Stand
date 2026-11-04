#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace ls::tools {

// Calls job(index) for every index from 0 to count - 1, spread over several threads.
// Each job must write only to its own slot, so the result never depends on the
// number of threads or on which thread ran which job.
template <typename Job>
void parallelFor(size_t count, int threadCount, Job&& job) {
    std::atomic<size_t> next{0};
    const auto worker = [&]() {
        for (size_t i = next++; i < count; i = next++) job(i);
    };
    std::vector<std::thread> threads;
    const int extra = std::max(threadCount, 1) - 1;
    for (int t = 0; t < extra; ++t) threads.emplace_back(worker);
    worker();  // The calling thread works too.
    for (std::thread& thread : threads) thread.join();
}

}  // namespace ls::tools
