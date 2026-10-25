// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "runtime_full.hpp"
#include "full_terrain.hpp"
#include "oa/formats/tnt.hpp"
#include "renderer_water.hpp"
#include <functional>

namespace oa::app::renderer_water_classic {

/// Water presentation through the existing Full scene, without a frame readback.
struct Inputs {
    const formats::tnt::Map* map{};
    full::SceneView view{};
    double seconds{};
    renderer_water::Wind wind{};
    float ripples{};
    float coast{};
    float shimmer{};
    float reflections{};
    const renderer_water::SurfaceCache* water_surface{};
    std::span<const renderer_water::Disturbance> disturbances{};
    /// Vertices already spent on reflections in this scene.
    std::size_t reflected_vertices{};
    /// True only for currently visible projected map pixels.
    std::function<bool(float, float)> visible;
};

/// Water ripples' motion of the map's own water art over the terrain's
/// picture from map pixel left, top to right, bottom: at each point of a
/// grid 8 to 16 window pixels apart, the broad swells and, up close, the
/// fine ripples of renderer_water_surface::art_offset at the water plane
/// the picture shows there, at the ripples' percent, none at the shore and
/// on land and all of it in deep water, faded out as the view zooms out
/// to 0.25. full_terrain draws the terrain's passes again moved by it.
/// Empty where the ripples are off or nothing would move.
full_terrain::ArtMotion
art_motion(const Inputs& in, double left, double top, double right, double bottom);

/// Append bounded water highlights before objects and the fog passes.
void surface(card::CardFrame& frame, const Inputs& in);

/// Shade submerged geometry and place a reflected naval silhouette before its original.
void objects(
    card::CardFrame& frame,
    Inputs& in,
    const WorldDrawList& draws,
    const WorldDraw& draw,
    std::size_t first_vertex,
    std::size_t first_index
);
} // namespace oa::app::renderer_water_classic
