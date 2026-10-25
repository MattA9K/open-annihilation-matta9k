// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "renderer_water.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace oa::app::renderer_water_surface {
/// One travelling wave of Water ripples' motion of the map's own water art.
/// The rotated view's vertex and fragment stages (shaders/live_scene_*.glsl)
/// repeat the same five waves.
struct ArtWave {
    float turn{};       ///< heading from the map's flow, radians
    float wavelength{}; ///< map pixels
    float speed{};      ///< share of the flow's texture travel the wave keeps
    float amplitude{};  ///< map pixels at full wind strength and 100%
    bool fine{};        ///< a fine ripple, shown only up close
};

/// Three broad swells and two fine ripples, each at its own heading, length
/// and speed, so that their sum never repeats as parallel bands; every crest
/// travels downstream along the map's flow.
inline constexpr std::array<ArtWave, 5> art_waves{{
    {0.0F, 71.0F, 0.65F, 1.6F, false},
    {0.66F, 113.0F, 0.28F, 1.2F, false},
    {-0.51F, 53.0F, 0.45F, 0.9F, false},
    {0.37F, 29.0F, 1.35F, 0.6F, true},
    {-0.82F, 23.0F, 1.1F, 0.45F, true},
}};

/// Water depth over which the art's motion grows from nothing at the shore
/// to its whole amplitude, so that the coast itself never moves.
inline constexpr float art_full_depth = 12.0F;

/// How much of the fine ripples a zoom shows: none below zoom 1, where the
/// classic view's moved mesh is too coarse for them, all from zoom 1.5.
inline float fine_detail(float zoom) {
    const float close = std::clamp((zoom - 1.0F) / 0.5F, 0.0F, 1.0F);
    return close * close * (3 - 2 * close);
}

/// How much of the motion a depth takes: smoothly from 0 at the waterline to
/// 1 at art_full_depth and below.
inline float art_depth_weight(float depth) {
    const float share = std::clamp(depth / art_full_depth, 0.0F, 1.0F);
    return share * share * (3 - 2 * share);
}

/// A wave's unit heading in the horizontal world plane.
inline std::array<float, 2> art_heading(const ArtWave& wave, const renderer_water::Wind& wind) {
    const float c = std::cos(wave.turn), s = std::sin(wave.turn);
    return {wind.x * c - wind.z * s, wind.x * s + wind.z * c};
}

/// How far Water ripples at 100% move the map's water art at a point of the
/// water plane, in map pixels across and down: each wave moves the art back
/// and forth along its heading as its crests travel downstream, at the
/// flow's integrated texture travel, so the motion is the same after a
/// saved game is loaded. `fine` scales the fine ripples (fine_detail).
inline std::array<float, 2>
art_offset(float x, float z, const renderer_water::Wind& wind, float fine) {
    std::array<float, 2> offset{};
    for (const auto& wave : art_waves) {
        const float amplitude = wave.amplitude * wind.strength * (wave.fine ? fine : 1.0F);
        if (amplitude == 0)
            continue;
        const auto heading = art_heading(wave, wind);
        const float travel = heading[0] * (x - wind.drift_x * wave.speed) +
                             heading[1] * (z - wind.drift_z * wave.speed);
        const float wave_shift =
            amplitude * std::sin(travel * (2 * std::numbers::pi_v<float>) / wave.wavelength);
        offset[0] += heading[0] * wave_shift;
        offset[1] += heading[1] * wave_shift;
    }
    return offset;
}
} // namespace oa::app::renderer_water_surface
