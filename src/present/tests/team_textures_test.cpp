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
    // Entries near the first two new colours, 0x39FF14 and 0x2E9B57.
    set(first_new, 57, 250, 20);
    set(second_new, 46, 155, 87);
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

} // namespace

int main() {
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
