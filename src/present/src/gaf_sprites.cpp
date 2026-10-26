// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/gaf_sprites.hpp"
#include "oa/present/player_colors.hpp"
#include "oa/present/rle.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <cstring>

namespace oa::present {

namespace {

constexpr size_t header_sequence_count = 0x04;
constexpr size_t header_reserved_after_sequence_count = 0x08;
constexpr size_t sequence_repeat_flags = 0x02;
constexpr size_t sequence_reserved_after_repeat_flags = 0x04;
constexpr size_t sequence_name = 0x08;
constexpr size_t slot_duration = 0x04;
constexpr size_t frame_height = 0x02;
constexpr size_t frame_origin_x = 0x04;
constexpr size_t frame_origin_y = 0x06;
constexpr size_t frame_key = 0x08;
constexpr size_t frame_encoding = 0x09;
constexpr size_t frame_child_count = 0x0A;
constexpr size_t frame_child_draw_mode = 0x0B;
constexpr size_t frame_reserved_after_child_draw_mode = 0x0C;
constexpr size_t frame_data = 0x10;
constexpr size_t offset_size = 4;
constexpr size_t row_length_size = 2;

// Row-RLE command bits (see rle.hpp).
constexpr uint8_t rle_skip = 0x01;
constexpr uint8_t rle_fill = 0x02;

// One walk of the file. The first walk (gaf null) checks every offset,
// counts the records and lists the header offsets; the second fills the
// records sized from those counts.
struct Relocation {
    std::span<const uint8_t> file;
    GafSprites* gaf = nullptr;
    size_t sequences = 0;
    size_t slots = 0;
    size_t headers = 0;
    size_t children = 0;
    std::vector<uint32_t> header_offsets;
};

uint8_t ascii_lower(char c) noexcept {
    const auto byte = static_cast<uint8_t>(c);
    return byte >= 'A' && byte <= 'Z' ? static_cast<uint8_t>(byte + ('a' - 'A')) : byte;
}

// Case-insensitive match of a NUL-padded name field against `name`.
bool name_matches(const char (&field)[gaf_name_size], const char* name) noexcept {
    for (int32_t i = 0; i < gaf_name_size; ++i) {
        if (ascii_lower(field[i]) != ascii_lower(name[i])) {
            return false;
        }
        if (field[i] == '\0') {
            return true;
        }
    }
    return name[gaf_name_size] == '\0';
}

bool fits(std::span<const uint8_t> file, size_t at, size_t size) noexcept {
    return at <= file.size() && size <= file.size() - at;
}

uint16_t u16_at(std::span<const uint8_t> file, size_t at) noexcept {
    return static_cast<uint16_t>(file[at] | (file[at + 1] << 8));
}

uint32_t u32_at(std::span<const uint8_t> file, size_t at) noexcept {
    return static_cast<uint32_t>(file[at]) | (static_cast<uint32_t>(file[at + 1]) << 8) |
           (static_cast<uint32_t>(file[at + 2]) << 16) |
           (static_cast<uint32_t>(file[at + 3]) << 24);
}

// Every row's commands must reach the frame width inside the row, so a
// clipped decode never reads past its row.
bool rows_fit(std::span<const uint8_t> file, size_t at, uint32_t width, uint32_t height) noexcept {
    for (uint32_t row = 0; row < height; ++row) {
        if (!fits(file, at, row_length_size)) {
            return false;
        }
        const size_t length = u16_at(file, at);
        at += row_length_size;
        if (!fits(file, at, length)) {
            return false;
        }
        const size_t end = at + length;
        uint32_t x = 0;
        while (length != 0 && x < width) {
            if (at >= end) {
                return false;
            }
            const uint8_t command = file[at++];
            if ((command & rle_skip) != 0) {
                x += command >> 1;
                continue;
            }
            const uint32_t count = std::min<uint32_t>((command >> 2) + 1, width - x);
            const size_t used = (command & rle_fill) != 0 ? 1 : count;
            if (end - at < used) {
                return false;
            }
            at += used;
            x += count;
        }
        at = end;
    }
    return true;
}

GafStatus relocate_header(Relocation& r, uint32_t at, bool child, Sprite*& out) {
    const std::span<const uint8_t> file = r.file;
    if (!fits(file, at, gaf_frame_header_size)) {
        return GafStatus::header_outside;
    }
    if (r.headers == gaf_max_headers) {
        return GafStatus::too_large;
    }
    Sprite* sprite = r.gaf != nullptr ? &r.gaf->sprites[r.headers] : nullptr;
    ++r.headers;
    if (sprite == nullptr) {
        r.header_offsets.push_back(at);
    }
    const uint16_t width = u16_at(file, at);
    const uint16_t height = u16_at(file, at + frame_height);
    const uint8_t encoding = file[at + frame_encoding];
    const uint8_t child_count = file[at + frame_child_count];
    const uint32_t data = u32_at(file, at + frame_data);
    if (child_count != 0) {
        if (child) {
            return GafStatus::nested_children;
        }
        if (!fits(file, data, child_count * offset_size)) {
            return GafStatus::children_outside;
        }
    } else if (encoding == OA_SPRITE_RAW) {
        if (!fits(file, data, static_cast<size_t>(width) * height)) {
            return GafStatus::pixels_outside;
        }
    } else if (sprite == nullptr && !rows_fit(file, data, width, height)) {
        return GafStatus::malformed_rows;
    }
    const size_t first_child = r.children;
    r.children += child_count;
    for (uint32_t i = 0; i < child_count; ++i) {
        Sprite* child_sprite = nullptr;
        const GafStatus status =
            relocate_header(r, u32_at(file, data + i * offset_size), true, child_sprite);
        if (status != GafStatus::ok) {
            return status;
        }
        if (sprite != nullptr) {
            r.gaf->children[first_child + i] = child_sprite;
        }
    }
    if (sprite != nullptr) {
        sprite->width = width;
        sprite->height = height;
        sprite->origin_x = static_cast<int16_t>(u16_at(file, at + frame_origin_x));
        sprite->origin_y = static_cast<int16_t>(u16_at(file, at + frame_origin_y));
        sprite->key = file[at + frame_key];
        sprite->encoding = encoding;
        sprite->child_count = child_count;
        sprite->child_draw_mode = file[at + frame_child_draw_mode];
        sprite->reserved_after_child_draw_mode =
            u32_at(file, at + frame_reserved_after_child_draw_mode);
        // A frame with no rows checks no data offset, and one past the file
        // points nowhere.
        if (child_count != 0)
            sprite->data = &r.gaf->children[first_child];
        else if (data <= r.gaf->bytes.size())
            sprite->data = r.gaf->bytes.data() + data;
        else
            sprite->data = nullptr;
        sprite->aux = nullptr;
        out = sprite;
    }
    return GafStatus::ok;
}

GafStatus relocate_sequence(Relocation& r, uint32_t at) {
    const std::span<const uint8_t> file = r.file;
    if (!fits(file, at, gaf_sequence_size)) {
        return GafStatus::sequence_outside;
    }
    const uint16_t frame_count = u16_at(file, at);
    if (!fits(
            file, at, gaf_sequence_size + static_cast<size_t>(frame_count) * gaf_frame_slot_size
        )) {
        return GafStatus::sequence_outside;
    }
    GafSequence* sequence = r.gaf != nullptr ? &r.gaf->sequences[r.sequences] : nullptr;
    ++r.sequences;
    const size_t first_slot = r.slots;
    r.slots += frame_count;
    for (uint32_t i = 0; i < frame_count; ++i) {
        const size_t slot_at = at + gaf_sequence_size + i * gaf_frame_slot_size;
        Sprite* frame = nullptr;
        const GafStatus status = relocate_header(r, u32_at(file, slot_at), false, frame);
        if (status != GafStatus::ok) {
            return status;
        }
        if (sequence != nullptr) {
            GafFrameSlot& slot = r.gaf->slots[first_slot + i];
            slot.frame = frame;
            slot.duration = u32_at(file, slot_at + slot_duration);
        }
    }
    if (sequence != nullptr) {
        sequence->frame_count = frame_count;
        sequence->repeat_flags = u16_at(file, at + sequence_repeat_flags);
        sequence->reserved_after_repeat_flags =
            u32_at(file, at + sequence_reserved_after_repeat_flags);
        std::memcpy(sequence->name, file.data() + at + sequence_name, gaf_name_size);
        sequence->frames = frame_count != 0 ? &r.gaf->slots[first_slot] : nullptr;
    }
    return GafStatus::ok;
}

GafStatus walk(Relocation& r) {
    const auto count = static_cast<int16_t>(u16_at(r.file, header_sequence_count));
    if (count <= 0) {
        return GafStatus::ok;
    }
    if (!fits(r.file, gaf_header_size, static_cast<size_t>(count) * offset_size)) {
        return GafStatus::short_header;
    }
    for (int32_t i = 0; i < count; ++i) {
        const GafStatus status =
            relocate_sequence(r, u32_at(r.file, gaf_header_size + i * offset_size));
        if (status != GafStatus::ok) {
            return status;
        }
    }
    return GafStatus::ok;
}

} // namespace

const char* gaf_status_text(GafStatus status) noexcept {
    switch (status) {
    case GafStatus::ok:
        return "ok";
    case GafStatus::too_large:
        return "GAF exceeds the loader size or header limit";
    case GafStatus::short_header:
        return "GAF header or sequence table is truncated";
    case GafStatus::sequence_outside:
        return "GAF sequence record or frame list lies outside the file";
    case GafStatus::header_outside:
        return "GAF frame header lies outside the file";
    case GafStatus::children_outside:
        return "GAF child table lies outside the file";
    case GafStatus::shared_header:
        return "GAF frame header is referenced more than once";
    case GafStatus::nested_children:
        return "GAF child frame has children of its own";
    case GafStatus::pixels_outside:
        return "GAF raw pixels lie outside the file";
    case GafStatus::malformed_rows:
        return "GAF row-RLE stream is truncated or a row does not cover its width";
    }
    return "unknown GAF status";
}

namespace {

/// Relocates a GAF, placing the file's bytes in `gaf` once they have passed
/// every check.
///
/// @param file GAF file bytes
/// @param[out] gaf relocated records; reset first
/// @param keep puts the file's bytes into gaf.bytes
/// @return ok, or the first check that failed
template <class Keep>
GafStatus relocate(std::span<const uint8_t> file, GafSprites& gaf, Keep&& keep) {
    gaf = GafSprites{};
    if (file.size() > gaf_max_bytes) {
        return GafStatus::too_large;
    }
    if (!fits(file, 0, gaf_header_size)) {
        return GafStatus::short_header;
    }
    Relocation check{};
    check.file = file;
    GafStatus status = walk(check);
    if (status != GafStatus::ok) {
        return status;
    }
    std::sort(check.header_offsets.begin(), check.header_offsets.end());
    if (std::adjacent_find(check.header_offsets.begin(), check.header_offsets.end()) !=
        check.header_offsets.end()) {
        return GafStatus::shared_header;
    }
    gaf.version = u32_at(file, 0);
    gaf.sequence_count = u32_at(file, header_sequence_count);
    gaf.reserved_after_sequence_count = u32_at(file, header_reserved_after_sequence_count);
    keep(gaf.bytes);
    gaf.sequences.resize(check.sequences);
    gaf.slots.resize(check.slots);
    gaf.sprites.resize(check.headers);
    gaf.children.resize(check.children);
    Relocation fill{};
    fill.file = std::span<const uint8_t>(gaf.bytes);
    fill.gaf = &gaf;
    status = walk(fill);
    return status;
}

} // namespace

GafStatus relocate_gaf(std::span<const uint8_t> file, GafSprites& gaf) {
    return relocate(file, gaf, [&](std::vector<uint8_t>& bytes) {
        bytes.assign(file.begin(), file.end());
    });
}

GafStatus relocate_gaf(std::vector<uint8_t>&& file, GafSprites& gaf) {
    return relocate(file, gaf, [&](std::vector<uint8_t>& bytes) { bytes = std::move(file); });
}

Sprite* gaf_frame(const GafSequence* sequence, int32_t index) noexcept {
    if (index < 0 || sequence == nullptr || index >= sequence->frame_count) {
        return nullptr;
    }
    return sequence->frames[index].frame;
}

const GafSequence* find_gaf_sequence(const GafSprites& gaf, const char* name) noexcept {
    if (name == nullptr) {
        return nullptr;
    }
    for (const GafSequence& sequence : gaf.sequences) {
        if (name_matches(sequence.name, name)) {
            return &sequence;
        }
    }
    return nullptr;
}

namespace {

/// Fills `opaque` with 1 where the frame draws a pixel and 0 where it is transparent.
///
/// @param gaf the archive the frame was relocated from
/// @param first the sequence's first frame
/// @param[out] opaque one byte per pixel
/// @return false when the frame is not raw or row-compressed, or its rows do not decode
bool frame_opacity(const GafSprites& gaf, const Sprite& first, std::vector<uint8_t>& opaque) {
    const size_t count = static_cast<size_t>(first.width) * first.height;
    opaque.assign(count, 0);
    if (first.child_count != 0 || first.data == nullptr)
        return false;
    if (first.encoding == OA_SPRITE_RAW) {
        const auto* source = static_cast<const uint8_t*>(first.data);
        for (size_t pixel = 0; pixel < count; ++pixel)
            opaque[pixel] = source[pixel] != first.key ? 1 : 0;
        return true;
    }
    if (first.encoding != OA_SPRITE_ROW_RLE)
        return false;
    const auto* bytes = static_cast<const uint8_t*>(first.data);
    const auto* file = gaf.bytes.data();
    const auto address = reinterpret_cast<uintptr_t>(bytes);
    const auto start = reinterpret_cast<uintptr_t>(file);
    if (file == nullptr || address < start || address >= start + gaf.bytes.size())
        return false;
    std::vector<uint8_t> decoded(count, first.key);
    const Rect32 rect{
        0, 0, static_cast<int32_t>(first.width) - 1, static_cast<int32_t>(first.height) - 1
    };
    const std::span<const uint8_t> stream(
        bytes, static_cast<size_t>(start + gaf.bytes.size() - address)
    );
    if (!decode_rle_rows(decoded.data(), first.width, rect, stream, rect))
        return false;
    for (size_t pixel = 0; pixel < count; ++pixel)
        opaque[pixel] = decoded[pixel] != first.key ? 1 : 0;
    return true;
}

} // namespace

void append_extra_radar_colors(GafSprites& gaf) {
    struct Pending {
        GafSequence* sequence = nullptr;
        const Sprite* first = nullptr;
        std::vector<uint8_t> opaque;
    };

    Pending pending[2]{};
    int pending_count = 0;
    for (const char* name : {"radlogo", "nuclogo"}) {
        GafSequence* sequence = nullptr;
        for (GafSequence& candidate : gaf.sequences) {
            if (name_matches(candidate.name, name)) {
                sequence = &candidate;
                break;
            }
        }
        if (sequence == nullptr || sequence->frames == nullptr ||
            sequence->frame_count != static_cast<uint16_t>(shipped_player_color_count))
            continue;
        const Sprite* first = sequence->frames[0].frame;
        if (first == nullptr || first->width == 0 || first->height == 0 || first->width > 256 ||
            first->height > 256)
            continue;
        if (!frame_opacity(gaf, *first, pending[pending_count].opaque))
            continue;
        pending[pending_count].sequence = sequence;
        pending[pending_count].first = first;
        ++pending_count;
    }
    if (pending_count == 0)
        return;
    const int extra = player_color_count - shipped_player_color_count;
    size_t pixel_bytes = 0;
    size_t slot_count = 0;
    for (int index = 0; index < pending_count; ++index) {
        pixel_bytes += pending[index].opaque.size() * static_cast<size_t>(extra);
        slot_count +=
            static_cast<size_t>(pending[index].sequence->frame_count) + static_cast<size_t>(extra);
    }
    gaf.added_pixels.clear();
    gaf.added_sprites.clear();
    gaf.added_slots.clear();
    gaf.added_pixels.reserve(pixel_bytes);
    gaf.added_sprites.reserve(static_cast<size_t>(pending_count) * static_cast<size_t>(extra));
    gaf.added_slots.reserve(slot_count);
    for (int index = 0; index < pending_count; ++index) {
        GafSequence& sequence = *pending[index].sequence;
        const Sprite& first = *pending[index].first;
        const size_t start = gaf.added_slots.size();
        const uint32_t duration = sequence.frames[0].duration;
        for (uint16_t frame = 0; frame < sequence.frame_count; ++frame)
            gaf.added_slots.push_back(sequence.frames[frame]);
        const std::vector<uint8_t>& opaque = pending[index].opaque;
        for (int color = shipped_player_color_count; color < player_color_count; ++color) {
            const uint8_t dot = player_colors[static_cast<size_t>(color)].dot;
            const size_t offset = gaf.added_pixels.size();
            gaf.added_pixels.resize(offset + opaque.size());
            uint8_t* pixels = gaf.added_pixels.data() + offset;
            for (size_t pixel = 0; pixel < opaque.size(); ++pixel)
                pixels[pixel] = opaque[pixel] != 0 ? dot : first.key;
            Sprite sprite{};
            sprite.width = first.width;
            sprite.height = first.height;
            sprite.origin_x = first.origin_x;
            sprite.origin_y = first.origin_y;
            sprite.key = first.key;
            sprite.encoding = OA_SPRITE_RAW;
            sprite.data = pixels;
            gaf.added_sprites.push_back(sprite);
            GafFrameSlot slot;
            slot.frame = &gaf.added_sprites.back();
            slot.duration = duration;
            gaf.added_slots.push_back(slot);
        }
        sequence.frames = &gaf.added_slots[start];
        sequence.frame_count = static_cast<uint16_t>(player_color_count);
    }
}

} // namespace oa::present
