// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Projectile contact on one thread and on several must leave the same state.
// The same seeded skirmish runs for several thousand ticks with the contact
// tests done on the calling thread alone and again shared across workers, and
// the two runs' match-state digests are compared after every tick.
#include "combat_fixture.hpp"

#include "oa/base/game_loop.hpp"
#include "oa/sim/state_hash.hpp"

#include <cstdint>
#include <stdexcept>
#include <iostream>
#include <thread>
#include <vector>

namespace {

using combat_fixture::Fixture;

#define OA_CHECK(cond)                                                                             \
    do {                                                                                           \
        if (!(cond))                                                                               \
            throw std::runtime_error(#cond);                                                       \
    } while (false)
using combat_fixture::Options;

constexpr uint32_t match_ticks = 4000;

/// Digests the match state as the savegame check does.
///
/// @param match match to digest
/// @return the match-state digest
uint64_t state_digest(oa::sim::match_runtime::Match& match) {
    const oa::base::game_loop::Timing timing{};
    return oa::sim::trace::match_state_hash(match, timing, 0, 0, nullptr);
}

/// Plays the skirmish for match_ticks and records the digest after each tick.
///
/// @param workers worker threads for the contact tests
/// @return one digest per tick, in order
std::vector<uint64_t> play(uint32_t workers) {
    Options options;
    options.contact_workers = workers;
    Fixture fixture(options);
    auto& gunner = fixture.spawn(0, 48, 48);
    auto& second = fixture.spawn(0, 48, 112);
    auto& target = fixture.spawn(1, 176, 64);
    auto& marcher = fixture.spawn(1, 208, 128);
    OA_CHECK(fixture.match->issue_attack(gunner.unit_index, target.unit_index, true));
    OA_CHECK(fixture.match->issue_attack(second.unit_index, target.unit_index, true));
    OA_CHECK(fixture.match->issue_attack(target.unit_index, gunner.unit_index, true));
    fixture.match->issue_ground_move(marcher.unit_index, {96 << 16, 0, 128 << 16}, false);
    std::vector<uint64_t> digests;
    digests.reserve(match_ticks);
    for (uint32_t tick = 0; tick < match_ticks; ++tick) {
        fixture.run(1);
        digests.push_back(state_digest(*fixture.match));
    }
    return digests;
}

} // namespace

int main() {
    try {
        const std::vector<uint64_t> alone = play(0);
        const uint32_t workers = std::max(2u, std::thread::hardware_concurrency());
        const std::vector<uint64_t> shared = play(workers);
        OA_CHECK(alone.size() == match_ticks);
        OA_CHECK(shared.size() == match_ticks);
        for (uint32_t tick = 0; tick < match_ticks; ++tick) {
            if (alone[tick] != shared[tick]) {
                std::cerr << "digest differs at tick " << tick + 1 << ": one thread 0x" << std::hex
                          << alone[tick] << ", " << std::dec << workers << " threads 0x" << std::hex
                          << shared[tick] << std::dec << '\n';
                return 1;
            }
        }
        std::cout << "contact threads agree over " << match_ticks << " ticks with 1 and "
                  << workers + 1 << " threads\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
