// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The frames the later player colours add to the ten-frame team sequences of
// logos.gaf, on a small made-up palette.

#include "oa/formats/gaf.hpp"
#include "oa/present/player_colors.hpp"
#include "oa/present/team_textures.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace {

int failures = 0;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

using oa::formats::gaf::Archive;
using oa::formats::gaf::Frame;
using oa::formats::gaf::Sequence;

constexpr uint8_t transparent = 9;
constexpr uint8_t grey = 20;
constexpr uint8_t blue = 227;
constexpr uint8_t red = 212;
constexpr uint8_t first_new = 30;
constexpr uint8_t second_new = 40;
constexpr int shipped = oa::present::shipped_player_color_count;
constexpr int total = oa::present::player_color_count;
constexpr uint32_t first_new_rgb = oa::present::player_colors[shipped].rgb;
constexpr uint32_t second_new_rgb = oa::present::player_colors[shipped + 1].rgb;

/// Returns one channel of a 0xRRGGBB colour: 0 red, 1 green, 2 blue.
constexpr int channel(uint32_t rgb, int which) {
    return static_cast<int>((rgb >> (16 - 8 * which)) & 0xff);
}

std::vector<uint8_t> made_up_palette() {
    std::vector<uint8_t> palette(256 * 4, 0);
    const auto set = [&](std::size_t index, int r, int g, int b) {
        palette[index * 4] = static_cast<uint8_t>(r);
        palette[index * 4 + 1] = static_cast<uint8_t>(g);
        palette[index * 4 + 2] = static_cast<uint8_t>(b);
    };
    set(grey, 200, 200, 200);
    set(blue, 23, 71, 231);
    set(red, 211, 43, 0);
    // The first two new colours themselves.
    set(first_new, channel(first_new_rgb, 0), channel(first_new_rgb, 1), channel(first_new_rgb, 2));
    set(second_new,
        channel(second_new_rgb, 0),
        channel(second_new_rgb, 1),
        channel(second_new_rgb, 2));
    return palette;
}

Frame picture(uint8_t first, uint8_t second, uint16_t height = 2) {
    Frame frame;
    frame.width = 2;
    frame.height = height;
    frame.origin_x = 1;
    frame.origin_y = 1;
    frame.transparency_index = transparent;
    frame.pixels.assign(static_cast<std::size_t>(2) * height, grey);
    frame.pixels[0] = first;
    frame.pixels[1] = second;
    frame.pixels[2] = transparent;
    frame.coverage.assign(frame.pixels.size(), 1);
    return frame;
}

Sequence team_sequence(const char* name, uint8_t blue_frame_pixel, uint8_t red_frame_pixel) {
    Sequence sequence;
    sequence.name = name;
    for (int color = 0; color < shipped; ++color) {
        if (color == 0)
            sequence.frames.push_back(picture(blue_frame_pixel, grey));
        else if (color == 1)
            sequence.frames.push_back(picture(red_frame_pixel, grey));
        else
            sequence.frames.push_back(picture(grey, grey, color == 2 ? 3 : 2));
    }
    return sequence;
}

void test_blue_frame_is_recoloured() {
    Archive archive;
    archive.sequences.push_back(team_sequence("logo", blue, grey));
    oa::present::append_extra_team_frames(archive, made_up_palette());
    const auto& frames = archive.sequences[0].frames;
    CHECK(frames.size() == static_cast<std::size_t>(total));
    // The shipped ten are untouched, the odd-sized one included.
    CHECK(frames[0].pixels[0] == blue);
    CHECK(frames[2].height == 3);
    for (int color = shipped; color < total; ++color) {
        const Frame& frame = frames[static_cast<std::size_t>(color)];
        CHECK(frame.width == 2 && frame.height == 2);
        CHECK(frame.origin_x == 1 && frame.origin_y == 1);
        CHECK(!frame.compressed && frame.layers.empty());
        CHECK(frame.transparency_index == transparent);
        CHECK(frame.pixels.size() == 4 && frame.coverage.size() == 4);
        // Neutral and transparent pixels stay as they were.
        CHECK(frame.pixels[1] == grey);
        CHECK(frame.pixels[2] == transparent);
    }
    // The first two new colours take the entries nearest to their rgb.
    CHECK(frames[10].pixels[0] == first_new);
    CHECK(frames[11].pixels[0] == second_new);
}

void test_red_frame_is_the_source_when_frame_zero_has_no_team_pixels() {
    Archive archive;
    archive.sequences.push_back(team_sequence("logo", grey, red));
    oa::present::append_extra_team_frames(archive, made_up_palette());
    const auto& frames = archive.sequences[0].frames;
    CHECK(frames.size() == static_cast<std::size_t>(total));
    CHECK(frames[10].pixels[0] == first_new);
    CHECK(frames[11].pixels[0] == second_new);
    CHECK(frames[10].pixels[1] == grey);
}

void test_other_sequences_are_left_alone() {
    Archive archive;
    archive.sequences.push_back(team_sequence("nine", blue, red));
    archive.sequences[0].frames.pop_back();
    archive.sequences.push_back(team_sequence("packed", blue, red));
    archive.sequences[1].frames[4].compressed = true;
    archive.sequences.push_back(team_sequence("layered", blue, red));
    archive.sequences[2].frames[4].layers.emplace_back();
    archive.sequences.push_back(team_sequence("short_pixels", blue, red));
    archive.sequences[3].frames[4].pixels.pop_back();
    oa::present::append_extra_team_frames(archive, made_up_palette());
    CHECK(archive.sequences[0].frames.size() == 9);
    CHECK(archive.sequences[1].frames.size() == static_cast<std::size_t>(shipped));
    CHECK(archive.sequences[2].frames.size() == static_cast<std::size_t>(shipped));
    CHECK(archive.sequences[3].frames.size() == static_cast<std::size_t>(shipped));
}

void test_second_call_and_bad_palette_change_nothing() {
    Archive archive;
    archive.sequences.push_back(team_sequence("logo", blue, red));
    oa::present::append_extra_team_frames(archive, std::vector<uint8_t>(1020, 0));
    CHECK(archive.sequences[0].frames.size() == static_cast<std::size_t>(shipped));
    oa::present::append_extra_team_frames(archive, {});
    CHECK(archive.sequences[0].frames.size() == static_cast<std::size_t>(shipped));
    oa::present::append_extra_team_frames(archive, made_up_palette());
    oa::present::append_extra_team_frames(archive, made_up_palette());
    CHECK(archive.sequences[0].frames.size() == static_cast<std::size_t>(total));
}

void test_team_picture_keeps_its_shade() {
    std::vector<uint8_t> palette = made_up_palette();
    // A blue half as light as `blue`, and half the first new colour.
    constexpr uint8_t dark_blue = 228;
    constexpr uint8_t dark_first_new = 31;
    const auto set = [&](std::size_t index, int r, int g, int b) {
        palette[index * 4] = static_cast<uint8_t>(r);
        palette[index * 4 + 1] = static_cast<uint8_t>(g);
        palette[index * 4 + 2] = static_cast<uint8_t>(b);
    };
    set(dark_blue, 12, 36, 116);
    set(dark_first_new,
        channel(first_new_rgb, 0) / 2,
        channel(first_new_rgb, 1) / 2,
        channel(first_new_rgb, 2) / 2);
    Sequence sequence = team_sequence("32xlogos", blue, grey);
    // The face is the blue the picture uses most; a compressed frame is read too.
    sequence.frames[0].pixels = {blue, dark_blue, transparent, blue};
    sequence.frames[0].compressed = true;
    sequence.frames[0].coverage = {1, 1, 0, 1};
    const auto made = oa::present::recolored_team_picture(sequence.frames, palette, shipped);
    CHECK(made.has_value());
    if (made) {
        CHECK(made->width == 2 && made->height == 2);
        CHECK(made->origin_x == 1 && made->origin_y == 1);
        CHECK(made->compressed && made->transparency_index == transparent);
        CHECK(made->pixels.size() == 4);
        CHECK(made->pixels[0] == first_new && made->pixels[3] == first_new);
        CHECK(made->pixels[1] == dark_first_new);
        CHECK(made->pixels[2] == transparent);
        CHECK(made->coverage == std::vector<uint8_t>({1, 1, 0, 1}));
    }
    // The shipped frames are read, not changed.
    CHECK(sequence.frames[0].pixels[0] == blue);
}

void test_team_picture_refuses_what_does_not_fit() {
    const std::vector<uint8_t> palette = made_up_palette();
    const Sequence plain = team_sequence("32xlogos", grey, grey);
    CHECK(!oa::present::recolored_team_picture(plain.frames, palette, shipped));
    const Sequence team = team_sequence("32xlogos", blue, red);
    CHECK(!oa::present::recolored_team_picture(team.frames, palette, 0));
    CHECK(!oa::present::recolored_team_picture(team.frames, palette, total));
    const std::vector<uint8_t> short_palette(1020, 0);
    CHECK(!oa::present::recolored_team_picture(team.frames, short_palette, shipped));
    CHECK(!oa::present::recolored_team_picture(
        std::span<const Frame>(team.frames.data(), 1), palette, shipped
    ));
    Sequence short_pixels = team;
    short_pixels.frames[0].pixels.pop_back();
    CHECK(!oa::present::recolored_team_picture(short_pixels.frames, palette, shipped));
    CHECK(oa::present::recolored_team_picture(team.frames, palette, total - 1).has_value());
}

} // namespace

int main() {
    test_team_picture_keeps_its_shade();
    test_team_picture_refuses_what_does_not_fit();
    test_blue_frame_is_recoloured();
    test_red_frame_is_the_source_when_frame_zero_has_no_team_pixels();
    test_other_sequences_are_left_alone();
    test_second_call_and_bad_palette_change_nothing();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
