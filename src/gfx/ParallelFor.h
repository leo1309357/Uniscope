/*
 * Copyright (c) 2026 Leo Jeang. All Rights Reserved.
 *
 * This is proprietary software. Unauthorized copying, modification,
 * or distribution is strictly prohibited.
 */

#ifndef UNISCOPE_D2_GFX_PARALLEL_FOR_H
#define UNISCOPE_D2_GFX_PARALLEL_FOR_H

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <thread>
#include <vector>

namespace uniscope::d2 {

// How many worker threads a CPU-side image job may use. One per hardware
// thread, capped so a many-core machine does not spawn dozens of threads to
// share a few megabytes of texels, and never fewer than one.
inline std::size_t parallelWorkerCount(std::size_t taskCount) {
    const unsigned hardware = std::thread::hardware_concurrency();
    const std::size_t workers = hardware == 0 ? 2 : static_cast<std::size_t>(hardware);
    return std::clamp<std::size_t>(std::min(workers, taskCount), 1, 16);
}

// Runs `fn(index)` for every index in [0, count) across the available hardware
// threads and returns when all of them are done. The indices are handed out
// from a shared counter rather than sliced in advance, so an uneven job (one
// tall atlas column next to many small cells) still keeps every thread busy
// until the last task starts.
//
// The calling thread works too, so a single-core machine runs the loop inline
// with no thread creation at all. `fn` must only write to memory that no other
// index writes to; the callers here each own a disjoint rectangle of one image.
//
// An exception thrown by any task is rethrown on the calling thread once every
// worker has stopped, so a failing task is reported as an exception rather than
// as a terminate() inside a thread the caller never sees.
template <typename Fn>
void parallelFor(std::size_t count, Fn&& fn) {
    if (count == 0) {
        return;
    }
    const std::size_t workers = parallelWorkerCount(count);
    if (workers <= 1) {
        for (std::size_t index = 0; index < count; ++index) {
            fn(index);
        }
        return;
    }

    std::atomic<std::size_t> next{0};
    std::atomic<bool> failed{false};
    std::exception_ptr error;
    std::atomic<bool> errorStored{false};
    const auto work = [&]() {
        while (!failed.load(std::memory_order_relaxed)) {
            const std::size_t index = next.fetch_add(1, std::memory_order_relaxed);
            if (index >= count) {
                return;
            }
            try {
                fn(index);
            } catch (...) {
                failed.store(true, std::memory_order_relaxed);
                bool expected = false;
                if (errorStored.compare_exchange_strong(expected, true)) {
                    error = std::current_exception();
                }
                return;
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(workers - 1);
    for (std::size_t worker = 1; worker < workers; ++worker) {
        threads.emplace_back(work);
    }
    work();
    for (std::thread& thread : threads) {
        thread.join();
    }
    if (error) {
        std::rethrow_exception(error);
    }
}

} // namespace uniscope::d2

#endif
