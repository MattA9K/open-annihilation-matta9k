// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The colours a player may take. There are twenty, in the order the menus
// step through them. The first ten are pictures shipped with the game:
// textures/logos.gaf sequence 32xlogos in the menus, and anims/FX.GAF
// sequences radlogo and nuclogo on the radar. The next ten are drawn from
// the values in this table.
//
// `dot` is an entry of palettes/palette.pal. The minimap, the megamap,
// whiteboard marks, shared cameras and the extra radar blips use it. The game
// shows 256 colours, so a dot is the entry nearest to the colour wanted.
// `rgb` is the colour wanted, as 0xRRGGBB. The menu square of a colour past
// the ten shipped frames is shaded from the entries of palettes/guipal.pal
// nearest to it, and so are the team panels of 3D units: each team texture of
// textures/logos.gaf gains a frame whose team pixels are the entries of
// palettes/palette.pal nearest to it (see team_textures.hpp). An `rgb` of 0
// on a shipped colour marks a picture that this table leaves in the file.
//
// To recolour a colour past the tenth, change its `rgb` for the menu square
// and the 3D team panels, and its `dot` for the maps and the radar. The first
// ten menu pictures stay the shipped frames; their dot indexes still colour
// the map.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace oa::present {

/// How many colours a player may take.
inline constexpr int player_color_count = 20;

/// How many of those colours are pictures shipped with the game.
inline constexpr int shipped_player_color_count = 10;

/// One player colour: its map dot and, past the shipped pictures, the colour it is drawn in.
struct PlayerColor {
    /// An index in palettes/palette.pal.
    uint8_t dot = 0;
    /// The colour wanted as 0xRRGGBB. 0 on a shipped picture.
    uint32_t rgb = 0;
};

/// The player colours, in the order the menus step through them.
inline constexpr std::array<PlayerColor, player_color_count> player_colors{{
    {227, 0},        // blue
    {212, 0},        // red
    {80, 0},         // white
    {235, 0},        // green
    {108, 0},        // dark blue
    {219, 0},        // purple
    {208, 0},        // yellow
    {93, 0},         // gray
    {130, 0},        // light blue
    {67, 0},         // khaki
    {250, 0x39FF14}, // neon green; the palette's pure green
    {36, 0x2E9B57},  // sea green
    {249, 0xE30B5D}, // raspberry; the palette has no pink-red, its pure red is nearest
    {153, 0x4B0082}, // indigo
    {210, 0xFF8102}, // orange
    {1, 0x800000},   // maroon
    {206, 0x670E10}, // royal maroon
    {19, 0xFF7E70},  // salmon; the palette's nearest rose
    {105, 0x0F52BA}, // sapphire blue
    {33, 0x40E0D0},  // turquoise
}};

/// The map-dot index of each player colour, in colour order.
inline constexpr auto player_dot_colors = [] {
    std::array<uint8_t, player_color_count> dots{};
    for (std::size_t index = 0; index < player_colors.size(); ++index)
        dots[index] = player_colors[index].dot;
    return dots;
}();

} // namespace oa::present
