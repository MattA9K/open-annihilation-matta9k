// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/match_runtime/construction_orders.hpp"
#include "oa/base/game_math.hpp"
#include <bit>
#include <cstdint>

namespace oa::sim::match_runtime {
namespace {
int32_t origin_cell(int32_t position, int32_t footprint, int32_t map_cells) {
    const auto biased =
        static_cast<uint32_t>(position) - static_cast<uint32_t>(footprint) * 0x80000u + 0x80000u;
    return base::game_math::fixed_extent_index(biased, map_cells, 20);
}
} // namespace

void snap_build_position(
    sim::ground_orders::Point& destination,
    int16_t footprint_x,
    int16_t footprint_z,
    int32_t map_cells_x,
    int32_t map_cells_z
) {
    const auto cell_x = origin_cell(destination[0], footprint_x, map_cells_x);
    const auto cell_z = origin_cell(destination[2], footprint_z, map_cells_z);
    // The centre is a 16.16 word. The multiply is unsigned, so a cell past
    // signed 16.16 keeps the centre's bits.
    destination[0] = static_cast<int32_t>(
        (static_cast<uint32_t>(footprint_x) + static_cast<uint32_t>(cell_x) * 2u) * 0x80000u
    );
    destination[2] = static_cast<int32_t>(
        (static_cast<uint32_t>(footprint_z) + static_cast<uint32_t>(cell_z) * 2u) * 0x80000u
    );
}

} // namespace oa::sim::match_runtime
