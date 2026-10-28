// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The worker pool cuts a batch into index-ordered runs and finishes all of
// them before run returns. The same items reduced in index order give the
// same total however many workers share the batch.
#include "oa/sim/job.hpp"

#include <cstdint>
#include <cstdio>

namespace {

int failures = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond);                        \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

constexpr uint32_t item_count = 1000;

struct Batch {
    uint32_t items[item_count]{};  ///< one slot per item
    uint32_t claims[item_count]{}; ///< how many runs wrote each item
    uint32_t calls{};              ///< runs that held an item
};

/// Writes each item of the run as its own index and counts the run.
void fill(void* context, oa::sim::job::WorkRange range) {
    auto* batch = static_cast<Batch*>(context);
    for (uint32_t index = range.begin; index < range.end; ++index) {
        batch->items[index] = index;
        batch->claims[index] += 1u;
    }
    batch->calls += 1u;
}

/// Sums the filled items in index order.
uint32_t total(const Batch& batch) {
    uint32_t sum = 0;
    for (uint32_t value : batch.items)
        sum += value;
    return sum;
}

/// Runs one batch and checks every item was written exactly once.
void check_batch(oa::sim::job::Pool& pool, uint32_t expected_runs) {
    Batch batch;
    pool.run(item_count, fill, &batch);
    CHECK(batch.calls == expected_runs);
    for (uint32_t index = 0; index < item_count; ++index) {
        CHECK(batch.items[index] == index);
        CHECK(batch.claims[index] == 1u);
    }
    CHECK(total(batch) == (item_count - 1u) * item_count / 2u);
}

} // namespace

int main() {
    oa::sim::job::Pool alone(0);
    CHECK(alone.workers() == 0u);
    CHECK(alone.threads() == 1u);
    check_batch(alone, 1u);
    // An empty batch does no work.
    {
        Batch batch;
        alone.run(0u, fill, &batch);
        CHECK(batch.calls == 0u);
    }

    oa::sim::job::Pool shared(3);
    CHECK(shared.workers() == 3u);
    CHECK(shared.threads() == 4u);
    // Several batches in a row, so a worker's view of the batch counter
    // has to move on with every one.
    for (uint32_t round = 0; round < 5u; ++round)
        check_batch(shared, 4u);

    // A batch smaller than the thread count leaves the spare workers idle
    // and still ready for the next one.
    {
        Batch batch;
        shared.run(2u, fill, &batch);
        CHECK(batch.calls == 2u);
        CHECK(batch.claims[0] == 1u);
        CHECK(batch.claims[1] == 1u);
    }
    check_batch(shared, 4u);

    // One more worker than the pool allows is clamped.
    oa::sim::job::Pool clamped(oa::sim::job::max_workers + 4u);
    CHECK(clamped.workers() == oa::sim::job::max_workers);

    // The single-threaded and the shared reduction name the same items.
    Batch first;
    Batch second;
    alone.run(item_count, fill, &first);
    shared.run(item_count, fill, &second);
    CHECK(total(first) == total(second));

    if (failures != 0) {
        std::fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    return 0;
}
