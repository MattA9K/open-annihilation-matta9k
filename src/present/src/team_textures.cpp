// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/team_textures.hpp"

#include "oa/present/player_colors.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace oa::present {

namespace {

constexpr std::size_t palette_entries = 256;
constexpr std::size_t palette_entry_bytes = 4;
constexpr std::size_t palette_bytes = palette_entries * palette_entry_bytes;

/// Palette entries from 240 up hold colours the game cycles or reserves. They
/// are never chosen for a new colour.
constexpr std::size_t first_reserved_entry = 240;

/// How far a team pixel's blue (frame 0) or red (frame 1) must lead the
/// other two channels.
constexpr int team_hue_lead = 32;

constexpr std::size_t first_team_frame = 0;
constexpr std::size_t second_team_frame = 1;

struct Rgb {
    int r = 0;
    int g = 0;
    int b = 0;
};

Rgb palette_color(std::span<const uint8_t> palette, std::size_t index) noexcept {
    const std::size_t at = index * palette_entry_bytes;
    return {palette[at], palette[at + 1], palette[at + 2]};
}

/// Returns the weight of a colour's light, from 0 to 255.
int luminance(const Rgb& color) noexcept {
    return (299 * color.r + 587 * color.g + 114 * color.b) / 1000;
}

/// Returns whether a frame is a plain picture of its own size.
bool raw_picture(const formats::gaf::Frame& frame) noexcept {
    return !frame.compressed && frame.layers.empty() && frame.width != 0 && frame.height != 0 &&
           frame.pixels.size() == static_cast<std::size_t>(frame.width) * frame.height;
}

/// Where a team pixel of a shipped frame lies in the palette.
enum class Source {
    blue_frame, ///< frame 0, the first colour
    red_frame,  ///< frame 1, the second colour
};

bool is_team_color(Source source, const Rgb& color) noexcept {
    if (source == Source::blue_frame)
        return color.b - std::max(color.r, color.g) >= team_hue_lead;
    return color.r - std::max(color.g, color.b) >= team_hue_lead;
}

/// Counts the pixels of a frame that carry the team's colour.
std::size_t team_pixels(
    const formats::gaf::Frame& frame, Source source, std::span<const uint8_t> palette
) noexcept {
    std::size_t count = 0;
    for (const uint8_t pixel : frame.pixels)
        if (pixel != frame.transparency_index &&
            is_team_color(source, palette_color(palette, pixel)))
            ++count;
    return count;
}

/// Returns the palette entry nearest to a colour among those a new colour may use.
uint8_t
nearest_entry(std::span<const uint8_t> palette, const Rgb& want, uint8_t transparent) noexcept {
    int best_distance = 0;
    std::size_t best = 0;
    bool found = false;
    for (std::size_t index = 1; index < first_reserved_entry; ++index) {
        if (index == transparent)
            continue;
        const Rgb have = palette_color(palette, index);
        const int dr = have.r - want.r;
        const int dg = have.g - want.g;
        const int db = have.b - want.b;
        const int distance = dr * dr + dg * dg + db * db;
        if (!found || distance < best_distance) {
            best_distance = distance;
            best = index;
            found = true;
        }
    }
    return static_cast<uint8_t>(best);
}

/// Returns a channel of the new colour for a pixel of the given weight of light.
///
/// `weight` is the pixel's light over the light of the colour it was drawn in,
/// in 1/1024. A darker pixel keeps the colour's hue at less strength; a
/// lighter one moves toward white by at most half the way.
int shaded_channel(int channel, int weight) noexcept {
    constexpr int one = 1024;
    if (weight <= one)
        return channel * weight / one;
    const int toward_white = std::min((weight - one) / 4, one / 2);
    return channel + (255 - channel) * toward_white / one;
}

/// Builds, for one new colour, the entry that replaces each team entry of a frame.
///
/// Entries that are not the team's colour map to themselves.
std::array<uint8_t, palette_entries> recolor_table(
    std::span<const uint8_t> palette,
    Source source,
    const PlayerColor& from,
    const PlayerColor& to,
    uint8_t transparent
) {
    std::array<uint8_t, palette_entries> table{};
    const Rgb base = palette_color(palette, from.dot);
    const int base_light = std::max(1, luminance(base));
    const Rgb target{
        static_cast<int>((to.rgb >> 16) & 0xff),
        static_cast<int>((to.rgb >> 8) & 0xff),
        static_cast<int>(to.rgb & 0xff)
    };
    for (std::size_t index = 0; index < palette_entries; ++index) {
        table[index] = static_cast<uint8_t>(index);
        const Rgb color = palette_color(palette, index);
        if (index == transparent || !is_team_color(source, color))
            continue;
        const int weight = luminance(color) * 1024 / base_light;
        const Rgb want{
            shaded_channel(target.r, weight),
            shaded_channel(target.g, weight),
            shaded_channel(target.b, weight)
        };
        table[index] = nearest_entry(palette, want, transparent);
    }
    return table;
}

} // namespace

void append_extra_team_frames(formats::gaf::Archive& archive, std::span<const uint8_t> palette) {
    if (palette.size() != palette_bytes)
        return;
    for (auto& sequence : archive.sequences) {
        auto& frames = sequence.frames;
        if (frames.size() != static_cast<std::size_t>(shipped_player_color_count))
            continue;
        if (!std::all_of(frames.begin(), frames.end(), raw_picture))
            continue;
        const std::size_t blue = team_pixels(frames[first_team_frame], Source::blue_frame, palette);
        const std::size_t red = team_pixels(frames[second_team_frame], Source::red_frame, palette);
        const bool from_blue = blue >= red;
        const Source source = from_blue ? Source::blue_frame : Source::red_frame;
        const formats::gaf::Frame shown = frames[from_blue ? first_team_frame : second_team_frame];
        const PlayerColor& from = player_colors[from_blue ? first_team_frame : second_team_frame];
        for (std::size_t color = shipped_player_color_count; color < player_colors.size();
             ++color) {
            const auto table = recolor_table(
                palette, source, from, player_colors[color], shown.transparency_index
            );
            formats::gaf::Frame extra = shown;
            for (uint8_t& pixel : extra.pixels)
                pixel = table[pixel];
            frames.push_back(std::move(extra));
        }
    }
}

} // namespace oa::present
