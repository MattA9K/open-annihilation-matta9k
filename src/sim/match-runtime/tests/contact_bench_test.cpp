// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Tick time of a match of about 2500 units, with the projectile contact tests
// on the calling thread alone and shared across workers. Every unit is ordered
// to attack the unit opposite it, so shots are in flight while the ticks are
// timed. The projectile pool holds 300 shots, so the contact pass is those
// shots. The test checks that both runs leave the same match-state digest, and
// prints the mean tick time.
#include "combat_fixture.hpp"

#include "oa/base/game_loop.hpp"
#include "oa/sim/state_hash.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using combat_fixture::Fixture;
using combat_fixture::Options;

constexpr uint32_t columns = 50;
constexpr uint32_t rows = 50;
constexpr uint32_t unit_count = columns * rows;
constexpr uint32_t spacing = 16; ///< world units between unit centres
constexpr uint32_t warmup_ticks = 10;
constexpr uint32_t timed_ticks = 40;

#define OA_CHECK(cond)                                                                             \
    do {                                                                                           \
        if (!(cond))                                                                               \
            throw std::runtime_error(#cond);                                                       \
    } while (false)

/// Fills the map with a grid of units, half owned by each player, each ordered
/// to attack the unit straight across the grid from it.
///
/// @param fixture the match
void spawn_grid(Fixture& fixture) {
    std::vector<uint16_t> slots;
    slots.reserve(unit_count);
    for (uint32_t row = 0; row < rows; ++row) {
        for (uint32_t column = 0; column < columns; ++column) {
            const uint8_t player = (column < columns / 2) ? 0 : 1;
            const auto* slot = fixture.match->create(
                {player,
                 1,
                 {(column * spacing + spacing / 2) << 16,
                  32u << 16,
                  (row * spacing + spacing / 2) << 16},
                 true,
                 1,
                 0}
            );
            OA_CHECK(slot != nullptr);
            slots.push_back(slot->unit_index);
        }
    }
    for (uint32_t index = 0; index < slots.size(); ++index) {
        const uint32_t row = index / columns;
        const uint32_t column = index % columns;
        const uint32_t opposite = row * columns + (columns - 1u - column);
        OA_CHECK(fixture.match->issue_attack(slots[index], slots[opposite], true));
    }
}

/// One timed run.
struct Run {
    double tick_ms{};      ///< mean milliseconds of one tick over the timed ticks
    uint64_t digest{};     ///< the match-state digest after the last tick
    int32_t projectiles{}; ///< projectiles in flight after the last tick
};

/// Spawns the grid, warms the match up, and times the following ticks.
///
/// @param workers worker threads for the contact tests
/// @return the mean tick time and the final digest
Run measure(uint32_t workers) {
    Options options;
    options.contact_workers = workers;
    options.per_player_limit = unit_count / 2;
    options.map_plots = columns * spacing / 16 + 2;
    Fixture fixture(options);
    spawn_grid(fixture);
    fixture.run(warmup_ticks);
    double tick_ms = 0;
    for (uint32_t tick = 0; tick < timed_ticks; ++tick) {
        const auto tick_start = std::chrono::steady_clock::now();
        fixture.run(1);
        tick_ms +=
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tick_start)
                .count();
    }
    const oa::base::game_loop::Timing timing{};
    return {
        tick_ms / timed_ticks,
        oa::sim::trace::match_state_hash(*fixture.match, timing, 0, 0, nullptr),
        fixture.match->state().game.projectile_count,
    };
}

} // namespace

int main() {
    try {
        const Run alone = measure(0);
        const uint32_t workers = std::max(2u, std::thread::hardware_concurrency());
        const Run shared = measure(workers);
        if (alone.digest != shared.digest) {
            std::cerr << "digest differs: 0x" << std::hex << alone.digest << " against 0x"
                      << shared.digest << std::dec << '\n';
            return 1;
        }
        std::cout << unit_count << " units, " << alone.projectiles
                  << " projectiles in flight, mean of " << timed_ticks << " ticks after "
                  << warmup_ticks << " warmup ticks\n"
                  << "1 thread:  " << alone.tick_ms << " ms/tick\n"
                  << workers + 1 << " threads: " << shared.tick_ms << " ms/tick\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
