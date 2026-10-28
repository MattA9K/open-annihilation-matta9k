// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A fixed pool of worker threads for one simulation tick.
//
// A tick gathers its inputs, hands each worker a contiguous run of items,
// waits until every run has been written, and then reads the results in an
// order the caller chooses. The pool decides nothing about the simulation:
// a worker only reads the memory the work function reads and writes the
// slots the caller gave it. The count of workers changes how many runs the
// items are cut into and never which item a run holds, so a result merged
// by item index is the same for every count.
#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>

namespace oa::sim::job {

/// The most worker threads one pool keeps.
inline constexpr uint32_t max_workers = 16;

/// One contiguous run of items inside a batch.
struct WorkRange {
    uint32_t begin{}; ///< first item index
    uint32_t end{};   ///< one past the last item index
};

/// Work over one run of a batch. The pool calls it on a worker thread.
using WorkFunction = void (*)(void* context, WorkRange range);

/// A fixed set of worker threads that run batches of indexed work.
///
/// The threads are started once, in the constructor, and joined in the
/// destructor. `run` cuts a batch into one run per worker plus the caller,
/// in index order, and returns only after every run has returned. With no
/// workers the caller runs the whole batch itself.
///
/// The work function must not call `run` on the same pool, must not throw,
/// and must write only to memory the caller reserved for its own items:
/// two runs never cover the same index.
class Pool {
  public:

    /// Starts a pool.
    ///
    /// @param workers worker threads to keep; clamped to max_workers. Zero
    ///        keeps none, and `run` does the work on the caller
    explicit Pool(uint32_t workers);

    /// Joins the worker threads. No batch may be running.
    ~Pool();

    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    /// Returns the worker threads this pool keeps.
    ///
    /// @return the count given to the constructor, after clamping
    [[nodiscard]] uint32_t workers() const noexcept { return workers_; }

    /// Returns how many threads share one batch: the workers plus the caller.
    ///
    /// @return at least one
    [[nodiscard]] uint32_t threads() const noexcept { return workers_ + 1u; }

    /// Runs one batch over `count` items and returns when all of it is done.
    ///
    /// Items are handed out in index order, in runs as equal as the item
    /// count allows: run `n` holds items `n * count / threads` up to
    /// `(n + 1) * count / threads`. The caller takes the first run and each
    /// worker the run after it, so the items a thread receives depend only
    /// on the thread count. A worker that has no items is not woken. Zero
    /// items returns without calling the function.
    ///
    /// @param count items in the batch
    /// @param function work over one run; called once per run that holds an item
    /// @param context passed back to every call
    void run(uint32_t count, WorkFunction function, void* context);

  private:

    struct Slot {
        WorkFunction function{}; ///< the batch's work, or null when the worker waits
        void* context{};         ///< the batch's context
        WorkRange range{};       ///< the items this worker runs
        bool done{true};         ///< the worker has finished its range
    };

    /// Parks until a batch hands this worker a run, runs it, and repeats.
    ///
    /// @param pool the owning pool
    /// @param index this worker's slot
    static void worker_main(Pool* pool, uint32_t index);

    uint32_t workers_{};                   ///< threads kept in reserve
    uint32_t batch_{};                     ///< batches started so far
    std::thread threads_[max_workers]{};   ///< the worker threads
    Slot slots_[max_workers]{};            ///< one slot per worker
    std::mutex gate_{};                    ///< guards the slots and the counters
    std::condition_variable work_ready_{}; ///< woken when a batch is posted
    std::condition_variable work_done_{};  ///< woken when a worker finishes
    bool stopping_{false};                 ///< the destructor has asked the threads to leave
};

} // namespace oa::sim::job
