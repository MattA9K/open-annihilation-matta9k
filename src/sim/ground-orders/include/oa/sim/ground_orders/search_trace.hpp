// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/sim/ground_orders/search_heap.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace oa::sim::ground_orders {

// One cell of the path search's map.
struct SearchMapCell {
    uint8_t flags{};
    uint8_t predecessor{};
    // The cell's open-set node. As wide as the heap's handles, so that an open
    // set of any size, whatever the node budget, keeps every handle.
    SearchHeap::Handle handle{};
};

static_assert(sizeof(SearchMapCell) == 8);
static_assert(sizeof(SearchMapCell::handle) == sizeof(SearchHeap::Handle));

/// Rebuilds a route from the finish cell back to the start through the predecessor directions.
///
/// Only cells where the direction changes are kept, in a ring of the last 64,
/// and the route is returned start first as footprint centres. A truncated map,
/// a direction past 7, a path leaving the map or a cycle gives no route. Only
/// the cells on the path are read.
///
/// @param width search map width in cells
/// @param height search map height in cells
/// @param start start cell
/// @param finish finish cell
/// @param footprint_x unit footprint width in cells
/// @param footprint_z unit footprint depth in cells
/// @param cells the search map, row by row; each cell's predecessor is 0..7
/// @return route points in integer world X/Z, at most 64; none for a broken map
std::vector<std::array<int16_t, 2>> reconstruct_search_path(
    uint32_t width,
    uint32_t height,
    std::array<int16_t, 2> start,
    std::array<int16_t, 2> finish,
    int16_t footprint_x,
    int16_t footprint_z,
    std::span<const SearchMapCell> cells
);
} // namespace oa::sim::ground_orders
