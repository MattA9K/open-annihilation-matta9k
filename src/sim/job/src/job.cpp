// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/job.hpp"

namespace oa::sim::job {

Pool::Pool(uint32_t workers) : workers_(workers > max_workers ? max_workers : workers) {
    for (uint32_t index = 0; index < workers_; ++index)
        threads_[index] = std::thread(worker_main, this, index);
}

Pool::~Pool() {
    {
        std::lock_guard<std::mutex> guard(gate_);
        stopping_ = true;
    }
    work_ready_.notify_all();
    for (uint32_t index = 0; index < workers_; ++index)
        threads_[index].join();
}

void Pool::worker_main(Pool* pool, uint32_t index) {
    Slot& slot = pool->slots_[index];
    std::unique_lock<std::mutex> guard(pool->gate_);
    uint32_t seen = 0;
    while (true) {
        pool->work_ready_.wait(guard, [&] { return pool->stopping_ || pool->batch_ != seen; });
        if (pool->stopping_)
            return;
        seen = pool->batch_;
        const WorkFunction function = slot.function;
        void* context = slot.context;
        const WorkRange range = slot.range;
        // A batch that left this worker idle still counts as seen: the
        // function is null and the slot is already done.
        guard.unlock();
        if (function != nullptr)
            function(context, range);
        guard.lock();
        slot.done = true;
        slot.function = nullptr;
        pool->work_done_.notify_one();
    }
}

void Pool::run(uint32_t count, WorkFunction function, void* context) {
    if (count == 0 || function == nullptr)
        return;
    const uint32_t threads = this->threads();
    const uint32_t runs = threads < count ? threads : count;
    {
        // The slots are published together with the batch counter, so a
        // worker that wakes always sees the run it is meant to take.
        std::lock_guard<std::mutex> guard(gate_);
        for (uint32_t index = 1; index < runs; ++index) {
            Slot& slot = slots_[index - 1u];
            slot.function = function;
            slot.context = context;
            slot.range = {index * count / runs, (index + 1u) * count / runs};
            slot.done = false;
        }
        ++batch_;
    }
    work_ready_.notify_all();
    function(context, {0u, count / runs});
    std::unique_lock<std::mutex> guard(gate_);
    work_done_.wait(guard, [&] {
        for (uint32_t index = 1; index < runs; ++index)
            if (!slots_[index - 1u].done)
                return false;
        return true;
    });
}

} // namespace oa::sim::job
