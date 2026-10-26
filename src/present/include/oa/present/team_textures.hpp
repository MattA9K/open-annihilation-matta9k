// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The team-coloured textures of textures/logos.gaf for the player colours the
// game does not ship a picture of. See player_colors.hpp.

#include "oa/formats/gaf.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace oa::present {

/// Gives every ten-frame team sequence of a logos archive a frame for each later player colour.
///
/// A team sequence holds one frame per shipped player colour. Frame 0 shows
/// the team's pixels in the first colour (blue) and frame 1 in the second
/// (red). A sequence that holds exactly ten frames, every one of them a raw
/// picture of the same size without layers, gains `player_color_count -
/// shipped_player_color_count` frames. Each is a copy of whichever of frames 0
/// and 1 has more team pixels. Its team pixels, the ones that are blue in
/// frame 0 or red in frame 1, become the palette entries nearest to the
/// colour's `rgb` in `player_colors`, darker or lighter as the pixel was. Every
/// other pixel, and the transparent ones, stay as they were. Other sequences,
/// a sequence of another frame count, and a sequence with a compressed or
/// layered frame are left as loaded. A second call finds twenty frames and
/// changes nothing.
///
/// @param[in,out] archive logos.gaf as parsed
/// @param palette palettes/palette.pal, 256 entries of four bytes; any other
///     size leaves the archive as loaded
void append_extra_team_frames(formats::gaf::Archive& archive, std::span<const uint8_t> palette);

} // namespace oa::present
