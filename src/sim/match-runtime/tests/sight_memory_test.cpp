// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A dying unit's line of sight in a running match: its stamp leaves its
// owner's coverage, and the viewer keeps seeing where its own unit died for
// 60 ticks until the frame loop lets it lapse. Also the map-feature sight
// test of two cell corners, and the two readings of a crowded cell.
#include "combat_fixture.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>

namespace {

using namespace combat_fixture;

// The fixture's 8x8 sight grid has 32-unit cells; units stamp their own cell.
uint16_t coverage_at(Fixture& f, uint8_t player, uint32_t x, uint32_t z) {
    return f.match->player_coverage(player)[static_cast<std::size_t>(z / 32 * 8 + x / 32)];
}

void viewer_remembers_its_dead_unit() {
    Fixture f;
    auto& own = f.spawn(0, 112, 112);
    auto& enemy = f.spawn(1, 240, 240);
    CHECK(coverage_at(f, 0, 112, 112) == 1);
    kill(f, own, enemy);
    // The unit's stamp is gone and the remembered one holds the cell.
    CHECK(coverage_at(f, 0, 112, 112) == 1);
    CHECK(f.match->point_visible(0, {112u << 16, 0, 112u << 16}));
    // Expiry is the death tick + 60 and lapses once the tick passes it.
    f.run(60);
    CHECK(coverage_at(f, 0, 112, 112) == 1);
    f.run(1);
    CHECK(coverage_at(f, 0, 112, 112) == 0);
    CHECK(!f.match->point_visible(0, {112u << 16, 0, 112u << 16}));
}

void enemy_dead_leave_nothing_behind() {
    Fixture f;
    auto& own = f.spawn(0, 16, 16);
    auto& enemy = f.spawn(1, 240, 240);
    CHECK(coverage_at(f, 1, 240, 240) == 1 && coverage_at(f, 0, 240, 240) == 0);
    kill(f, enemy, own);
    CHECK(coverage_at(f, 1, 240, 240) == 0 && coverage_at(f, 0, 240, 240) == 0);
    f.run(1);
    CHECK(coverage_at(f, 0, 16, 16) == 1);
}

void feature_cells_seen_by_either_corner() {
    Fixture f;
    f.spawn(0, 112, 112);
    // Sight cell (3,3) is world 96..127; map cells are 16 units.
    CHECK(f.match->cell_or_footprint_corner_shown(0, 6, 6, 1, 1, 0));
    CHECK(f.match->cell_or_footprint_corner_shown(0, 4, 4, 2, 2, 0));
    CHECK(!f.match->cell_or_footprint_corner_shown(0, 4, 4, 1, 1, 0));
    // The plot height raises the point by half of it: Z 128 - 32 is row 3.
    CHECK(f.match->cell_or_footprint_corner_shown(0, 6, 8, 0, 0, 64));
    CHECK(!f.match->cell_or_footprint_corner_shown(0, 6, 8, 0, 0, 0));
    CHECK(!f.match->cell_or_footprint_corner_shown(1, 6, 6, 1, 1, 0));
}

// A cell 256 stamps see is out of the simulation's sight, as 3.1c's byte
// count reads it, but the player's view shows it and the unit standing in it.
void crowded_cell_shown_to_the_player() {
    Fixture f;
    auto& enemy = f.spawn(1, 112, 112);
    auto& grid = f.match->sight_mutable();
    std::fill(grid.coverage.begin(), grid.coverage.end(), uint16_t{256});
    const std::array<uint32_t, 3> point{112u << 16, 0, 112u << 16};
    CHECK(!f.match->point_visible(0, point) && f.match->point_shown(0, point));
    CHECK(!f.match->unit_visible(0, enemy.unit_index));
    CHECK(f.match->unit_shown(0, enemy.unit_index));
    CHECK(f.match->cell_or_footprint_corner_shown(0, 6, 6, 1, 1, 0));
    // One stamp more and the simulation sees the cell again.
    std::fill(grid.coverage.begin(), grid.coverage.end(), uint16_t{257});
    CHECK(f.match->point_visible(0, point) && f.match->unit_visible(0, enemy.unit_index));
}

} // namespace

int main() {
    viewer_remembers_its_dead_unit();
    enemy_dead_leave_nothing_behind();
    feature_cells_seen_by_either_corner();
    crowded_cell_shown_to_the_player();
    std::cout << "sight memory passed\n";
}
