// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/ground_orders/navigator.hpp"
#include <algorithm>
#include <bit>

namespace oa::sim::ground_orders {
namespace {
Fixed from_pixel(int32_t pixel) noexcept {
    return std::bit_cast<Fixed>(static_cast<uint32_t>(pixel) << 16);
}
} // namespace

void set_mirrored_goal(MirroredNavigation& n, Goal* goal) noexcept {
    if (n.goal && n.goal->order)
        n.goal->order->raised_events |= goal_replaced_event;
    n.goal = goal;
}

std::array<Point, 3> mirrored_steering_points(const MirroredNavigation& n) noexcept {
    std::array<Point, 3> out{};
    if (n.count <= 0)
        return out;
    const auto last = std::min<int32_t>(n.count, route_delta_capacity) - 1;
    for (int32_t i = 0; i < 3; ++i) {
        const auto& p = n.points[static_cast<std::size_t>(std::min(i, last))];
        out[static_cast<std::size_t>(i)] = {from_pixel(p[0]), 0, from_pixel(p[1])};
    }
    return out;
}
} // namespace oa::sim::ground_orders
