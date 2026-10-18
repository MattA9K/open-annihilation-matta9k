// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A wreck is picked where it is drawn: the wrecks of a destroyed Skeeter and
// a destroyed Crusader sinking to the sea bed of Coast To Coast and a
// destroyed Stumpy's wreck on its high ground, hovered through synthetic SDL
// pointer motion with a construction ship and a construction kbot selected,
// then clicked, with pictures of the cursor over each and of the Reclaim
// given.
#include "oa/app/runtime.hpp"
#include "oa/core/map_plot.h"
#include "oa/formats/png.hpp"
#include "oa/sim/feature_runtime.hpp"
#include "oa/sim/gameplay_input/order_cursor.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace oa::app {
namespace {

namespace input = oa::sim::gameplay_input;

constexpr std::string_view kMapName = "Coast To Coast";
// Cells of open water around the sea site, and of level high ground around
// the hill site, each way from the middle cell.
constexpr int32_t kSeaSiteCells = 6;
constexpr int32_t kHillSiteCells = 3;
// The least depth of the sea bed below the sea and the least height of the
// hill, in map heights, and the most the hill's heights may differ.
constexpr int32_t kLeastDepth = 40;
constexpr int32_t kLeastHillHeight = 80;
constexpr int32_t kMostHillSlope = 8;
// The ships sunk, each over its own site of open water so many cells apart,
// the first drawn into the pictures.
constexpr std::array<const char*, 2> kSunkShips{"ARMPT", "ARMROY"};
constexpr int32_t kSeaSitesApartCells = 24;
// Map pixels east of the doomed unit that its reclaimer is placed at.
constexpr int32_t kReclaimerApart = 72;
// Ticks a dead unit has to leave its wreck, the wreck to come to rest, and
// the reclaimer to take it.
constexpr int kWreckTicks = 300;
constexpr int kSinkTicks = 1200;
constexpr int kReclaimTicks = 3000;
// Cells around a dead unit searched for its wreck.
constexpr int32_t kWreckSearchCells = 6;
// Screen pixels between pointer samples, and kept clear of a footprint's
// drawn edges, where rounding and the slope of the ground decide.
constexpr int32_t kSampleStep = 2;
constexpr int32_t kEdgeMargin = 4;
// Screen pixels west of a clicked wreck the pointer is moved to for the
// picture of the order given, so that it does not hide the order's marker.
constexpr int32_t kOrderPictureApart = 64;
// The Reclaim order's kind.
constexpr uint8_t kReclaimKind = 32;

[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error("wreck pick check: " + what);
}

/// Writes a frame as a PNG file.
///
/// @param path the file
/// @param frame the frame
void write_png(const std::string& path, const oa::present::world_renderer::Surface& frame) {
    std::vector<uint8_t> file;
    const oa::formats::png::Header header{
        frame.width, frame.height, 8, oa::formats::png::ColorType::rgb, {}
    };
    if (!oa::formats::png::write(oa::formats::png::Image{header, {}, frame.rgb}, &file))
        fail("cannot encode " + path);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(
        reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size())
    );
    if (!output)
        fail("cannot write " + path);
}

const char* cursor_name(input::OrderCursor cursor) {
    switch (cursor) {
    case input::OrderCursor::reclaim:
        return "Reclaim";
    case input::OrderCursor::move:
        return "Move";
    case input::OrderCursor::select:
        return "Select";
    case input::OrderCursor::normal:
        return "Normal";
    default:
        return "another cursor";
    }
}

/// A wreck's footprint origin and size in cells, the height it is drawn at
/// and its feature's name.
struct WreckSite {
    int32_t cell_x{};
    int32_t cell_z{};
    int32_t footprint_x{};
    int32_t footprint_z{};
    int32_t drawn_height{};
    std::string name;
};

} // namespace

void Runtime::check_wreck_pick() {
    namespace features = oa::sim::feature_runtime;
    if (sdl_.renderer == nullptr || sdl_.window == nullptr)
        fail("needs the SDL renderer");
    exercise_click(menu::resource_name(menu::Button::single_player));
    exercise_click(entry::resource_name(entry::Button::skirmish));
    skirmish_settings_.map_name = std::string(kMapName);
    if (select_map(kMapName) == 0)
        fail("cannot select " + std::string(kMapName));
    state_.player_count = 2;
    exercise_click(skirmish::resource_name(skirmish::Button::start));
    if (screen_ != Screen::match || !match_)
        fail("the skirmish on " + std::string(kMapName) + " did not start");
    apply_output_mode();
    auto& world = match_->state();
    auto& slots = match_->world().slots;
    const int32_t sea = world.game.sea_level;
    const int32_t map_width = world.game.map_width;
    const int32_t map_height = world.game.map_height;

    bool running = true;
    const auto send = [&](SDL_EventType type, uint8_t button, float x, float y) {
        float window_x = 0.0F;
        float window_y = 0.0F;
        if (!frame_to_window(sdl_.renderer, x, y, &window_x, &window_y))
            fail(SDL_GetError());
        SDL_Event event{};
        event.type = type;
        if (type == SDL_EVENT_MOUSE_MOTION) {
            event.motion.windowID = SDL_GetWindowID(sdl_.window);
            event.motion.x = window_x;
            event.motion.y = window_y;
        } else {
            event.button.windowID = SDL_GetWindowID(sdl_.window);
            event.button.button = button;
            event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.clicks = 1;
            event.button.x = window_x;
            event.button.y = window_y;
        }
        dispatch_event(event, running);
    };
    const auto click = [&](float x, float y) {
        send(SDL_EVENT_MOUSE_MOTION, 0, x, y);
        send(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, x, y);
        send(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, x, y);
    };
    const auto viewport = [&] { return live_viewport(match_camera_x_, match_camera_z_); };
    const auto plot_height = [&](int32_t x, int32_t z) {
        const auto* plot = oa::world_plot(&world, x, z);
        return plot != nullptr ? static_cast<int32_t>(plot->height) : -1;
    };
    const auto on_battlefield = [&](int32_t x, int32_t y) {
        return x >= match_layout_.left && y >= match_layout_.top &&
               x < match_layout_.left + match_layout_.battlefield_width() &&
               y < match_layout_.top + match_layout_.battlefield_height() &&
               !radar_contains(static_cast<float>(x), static_cast<float>(y));
    };
    // Cells the view centred on a site keeps on the map.
    const auto margin_x = visible_map_width() / (2 * OA_MAP_CELL_PIXELS) + kSeaSiteCells + 2;
    const auto margin_z = visible_map_height() / (2 * OA_MAP_CELL_PIXELS) + kSeaSiteCells + 2;

    // The depth of open water around each cell the view can centre on, and
    // the highest level ground.
    struct SeaSite {
        int32_t depth{};
        int32_t x{};
        int32_t z{};
    };

    std::vector<SeaSite> sea_sites;
    std::optional<std::array<int32_t, 2>> hill_site;
    int32_t hill_height = 0;
    for (int32_t z = margin_z; z < map_height - margin_z; ++z)
        for (int32_t x = margin_x; x < map_width - margin_x; ++x) {
            int32_t deepest_bed = 0;
            bool open_water = true;
            for (int32_t dz = -kSeaSiteCells; open_water && dz <= kSeaSiteCells; ++dz)
                for (int32_t dx = -kSeaSiteCells; open_water && dx <= kSeaSiteCells; ++dx) {
                    const auto* plot = oa::world_plot(&world, x + dx, z + dz);
                    open_water = plot != nullptr && plot->feature >= OA_PLOT_FEATURE_RESERVED &&
                                 sea - static_cast<int32_t>(plot->height) >= kLeastDepth;
                    if (open_water)
                        deepest_bed = std::max(deepest_bed, static_cast<int32_t>(plot->height));
                }
            if (open_water)
                sea_sites.push_back({sea - deepest_bed, x, z});
            int32_t lowest = 255;
            int32_t highest = 0;
            bool level = true;
            for (int32_t dz = -kHillSiteCells; level && dz <= kHillSiteCells; ++dz)
                for (int32_t dx = -kHillSiteCells; level && dx <= kHillSiteCells; ++dx) {
                    const auto* plot = oa::world_plot(&world, x + dx, z + dz);
                    level = plot != nullptr && plot->feature >= OA_PLOT_FEATURE_RESERVED &&
                            plot->ground_unit == 0;
                    if (level) {
                        lowest = std::min(lowest, static_cast<int32_t>(plot->height));
                        highest = std::max(highest, static_cast<int32_t>(plot->height));
                        level = highest - lowest <= kMostHillSlope;
                    }
                }
            if (level && lowest >= kLeastHillHeight && lowest > hill_height) {
                hill_height = lowest;
                hill_site = std::array<int32_t, 2>{x, z};
            }
        }
    // The deepest site for each ship, each well apart from the sites
    // taken before it.
    std::stable_sort(sea_sites.begin(), sea_sites.end(), [](const auto& a, const auto& b) {
        return a.depth > b.depth;
    });
    std::vector<SeaSite> ship_sites;
    for (const auto& site : sea_sites) {
        if (ship_sites.size() == kSunkShips.size())
            break;
        if (std::all_of(ship_sites.begin(), ship_sites.end(), [&](const auto& taken) {
                return std::max(std::abs(site.x - taken.x), std::abs(site.z - taken.z)) >=
                       kSeaSitesApartCells;
            }))
            ship_sites.push_back(site);
    }
    if (ship_sites.size() != kSunkShips.size())
        fail(
            "found no " + std::to_string(kSunkShips.size()) + " sites of open water at least " +
            std::to_string(kLeastDepth) + " deep"
        );
    if (!hill_site)
        fail("found no level ground at least " + std::to_string(kLeastHillHeight) + " high");
    std::cout << "wreck pick check: " << kMapName << ", sea level " << sea << "; level ground "
              << hill_height << " high at cell " << (*hill_site)[0] << "," << (*hill_site)[1]
              << "\n";

    const auto place = [&](const char* name, int32_t x, int32_t z) -> uint16_t {
        const auto type = oa::sim::unit_spawn::find_type_index(spawn_type_names_, name);
        if (type == 0)
            fail(std::string("the game has no ") + name);
        oa::sim::unit_spawn::Request request;
        request.player = match_local_player_;
        request.type = type;
        request.finished = true;
        request.state = kGroundOccupancyState;
        const auto ground = std::max(
            match_->map_height(static_cast<uint32_t>(x) << 16U, static_cast<uint32_t>(z) << 16U),
            sea
        );
        request.position = {
            static_cast<uint32_t>(x) << 16U,
            static_cast<uint32_t>(ground) << 16U,
            static_cast<uint32_t>(z) << 16U
        };
        auto* slot = match_->create(request);
        if (slot == nullptr || slot->unit == nullptr)
            fail(std::string("could not place ") + name);
        slot->unit->object_present = true;
        return slot->unit_index;
    };
    // The unit dies to a light last hit, as one finished off by a weapon,
    // so that it leaves its wreck; its wreck is the object feature whose
    // record is found near its last cell.
    const auto destroy = [&](uint16_t unit) -> WreckSite {
        auto& slot = slots[unit];
        const auto cell_x = static_cast<int32_t>(slot.unit->position[0] >> 20);
        const auto cell_z = static_cast<int32_t>(slot.unit->position[2] >> 20);
        slot.unit->health = 1;
        slot.unit->previous_health_percent = 0;
        match_->apply_damage_event(slot, nullptr, 2, 1, 0);
        for (int tick = 0; tick < kWreckTicks; ++tick) {
            step_match_simulation();
            for (int32_t dz = -kWreckSearchCells; dz <= kWreckSearchCells; ++dz)
                for (int32_t dx = -kWreckSearchCells; dx <= kWreckSearchCells; ++dx) {
                    const auto* plot = oa::world_plot(&world, cell_x + dx, cell_z + dz);
                    if (plot == nullptr || (plot->flags & OA_PLOT_FLAG_ANIMATING_FEATURE) == 0 ||
                        plot->feature >= world.feature_def_count)
                        continue;
                    const auto& def = world.feature_defs[plot->feature];
                    if ((def.flags & OA_FEATURE_FLAG_SPRITE) != 0)
                        continue;
                    return WreckSite{
                        cell_x + dx,
                        cell_z + dz,
                        def.footprint_x,
                        def.footprint_z,
                        0,
                        {def.name, ::strnlen(def.name, sizeof def.name)}
                    };
                }
        }
        fail("the destroyed unit left no wreck within " + std::to_string(kWreckTicks) + " ticks");
    };
    const auto wreck_record = [&](const WreckSite& wreck) -> const features::PlacedFeature* {
        const auto* plot = oa::world_plot(&world, wreck.cell_x, wreck.cell_z);
        if (plot == nullptr || (plot->flags & OA_PLOT_FLAG_ANIMATING_FEATURE) == 0)
            return nullptr;
        return features::feature_record(world, plot->feature_record);
    };
    const auto centre_view = [&](const WreckSite& wreck) {
        match_camera_x_ = wreck.cell_x * OA_MAP_CELL_PIXELS +
                          wreck.footprint_x * OA_MAP_CELL_PIXELS / 2 - visible_map_width() / 2;
        match_camera_z_ = wreck.cell_z * OA_MAP_CELL_PIXELS +
                          wreck.footprint_z * OA_MAP_CELL_PIXELS / 2 - visible_map_height() / 2;
        render_match_surface();
    };
    // Where a map point at a height is drawn on the screen.
    const auto screen_of = [&](int32_t map_x, int32_t height, int32_t map_z) {
        return project_match_point(
            viewport(),
            {static_cast<uint32_t>(map_x) << 16U,
             static_cast<uint32_t>(std::max(height, 0)) << 16U,
             static_cast<uint32_t>(map_z) << 16U}
        );
    };
    // The screen rectangle a footprint covers drawn at a height:
    // left, top, right and bottom, the last two past its edge.
    const auto footprint_box = [&](const WreckSite& wreck, int32_t height) {
        const auto top_left =
            screen_of(wreck.cell_x * OA_MAP_CELL_PIXELS, height, wreck.cell_z * OA_MAP_CELL_PIXELS);
        const auto bottom_right = screen_of(
            (wreck.cell_x + wreck.footprint_x) * OA_MAP_CELL_PIXELS,
            height,
            (wreck.cell_z + wreck.footprint_z) * OA_MAP_CELL_PIXELS
        );
        return std::array<int32_t, 4>{top_left.x, top_left.y, bottom_right.x, bottom_right.y};
    };
    const auto inside = [](const std::array<int32_t, 4>& box, int32_t x, int32_t y) {
        return x >= box[0] + kEdgeMargin && y >= box[1] + kEdgeMargin && x < box[2] - kEdgeMargin &&
               y < box[3] - kEdgeMargin;
    };
    const auto near_edge = [](const std::array<int32_t, 4>& box, int32_t x, int32_t y) {
        const bool in_outer = x >= box[0] - kEdgeMargin && y >= box[1] - kEdgeMargin &&
                              x < box[2] + kEdgeMargin && y < box[3] + kEdgeMargin;
        const bool in_inner = x >= box[0] + kEdgeMargin && y >= box[1] + kEdgeMargin &&
                              x < box[2] - kEdgeMargin && y < box[3] - kEdgeMargin;
        return in_outer && !in_inner;
    };
    const auto hover = [&](int32_t x, int32_t y) {
        send(SDL_EVENT_MOUSE_MOTION, 0, static_cast<float>(x), static_cast<float>(y));
        return static_cast<input::OrderCursor>(pick_match_cursor());
    };
    // The battlefield as drawn with the pointer's cursor over it.
    const auto picture_with_cursor = [&](const std::string& path) {
        render_match_surface();
        oa::present::world_renderer::Surface picture{
            match_world_cpu_.width, match_world_cpu_.height, match_world_cpu_.rgb
        };
        auto drawn_view = viewport();
        const auto left = drawn_view.destination_x;
        const auto top = drawn_view.destination_y;
        drawn_view.destination_x = 0;
        drawn_view.destination_y = 0;
        drawn_view.surface_width = picture.width;
        drawn_view.surface_height = picture.height;
        std::ignore = draw_order_overlays(picture, drawn_view);
        select_game_cursor(pick_match_cursor());
        if (cursor_image_ != nullptr)
            if (const auto rendered = oa::formats::gaf::render_normal(*cursor_image_);
                rendered.ok()) {
                const auto& frame = *rendered.frame;
                const auto& palette =
                    match_palette_.size() >= 1024 ? match_palette_ : resources_.gui_palette;
                const auto origin_x = static_cast<int32_t>(pointer_x_) - left - frame.origin_x;
                const auto origin_y = static_cast<int32_t>(pointer_y_) - top - frame.origin_y;
                for (std::size_t row = 0; row < frame.height; ++row)
                    for (std::size_t column = 0; column < frame.width; ++column) {
                        const auto offset = row * frame.width + column;
                        if (offset >= frame.coverage.size() || frame.coverage[offset] == 0)
                            continue;
                        const auto x = origin_x + static_cast<int32_t>(column);
                        const auto y = origin_y + static_cast<int32_t>(row);
                        const auto entry = static_cast<std::size_t>(frame.pixels[offset]) * 4U;
                        if (x < 0 || y < 0 || x >= static_cast<int32_t>(picture.width) ||
                            y >= static_cast<int32_t>(picture.height) ||
                            entry + 2 >= palette.size())
                            continue;
                        const auto at = (static_cast<std::size_t>(y) * picture.width +
                                         static_cast<std::size_t>(x)) *
                                        3U;
                        picture.rgb[at] = palette[entry];
                        picture.rgb[at + 1] = palette[entry + 1];
                        picture.rgb[at + 2] = palette[entry + 2];
                    }
            }
        write_png(path, picture);
    };

    // Every pointer sample over the wreck's footprint as drawn must show
    // Reclaim, and each over where the footprint would be drawn at the
    // height the pointer meets, but not over the drawn one, Move. Returns
    // the failures, after counting both.
    struct Samples {
        std::size_t drawn_reclaim{};
        std::size_t drawn_other{};
        std::size_t ground_only_move{};
        std::size_t ground_only_reclaim{};
        std::optional<std::array<int32_t, 2>> drawn_miss;
        std::optional<std::array<int32_t, 2>> ground_only_hit;
    };

    const auto sample = [&](const WreckSite& wreck, int32_t ground_height) {
        Samples counted;
        const auto drawn = footprint_box(wreck, wreck.drawn_height);
        const auto ground = footprint_box(wreck, ground_height);
        for (int32_t y = std::min(drawn[1], ground[1]); y < std::max(drawn[3], ground[3]);
             y += kSampleStep)
            for (int32_t x = drawn[0]; x < drawn[2]; x += kSampleStep) {
                if (!on_battlefield(x, y) || near_edge(drawn, x, y) || near_edge(ground, x, y))
                    continue;
                const bool over_drawn = inside(drawn, x, y);
                const bool over_ground = inside(ground, x, y);
                if (!over_drawn && !over_ground)
                    continue;
                const auto cursor = hover(x, y);
                if (hovered_ || hovered_match_unit_ != 0)
                    continue;
                if (over_drawn) {
                    if (cursor == input::OrderCursor::reclaim) {
                        ++counted.drawn_reclaim;
                    } else {
                        ++counted.drawn_other;
                        if (!counted.drawn_miss || y > (*counted.drawn_miss)[1])
                            counted.drawn_miss = std::array<int32_t, 2>{x, y};
                    }
                } else if (cursor == input::OrderCursor::reclaim) {
                    ++counted.ground_only_reclaim;
                    if (!counted.ground_only_hit || y < (*counted.ground_only_hit)[1])
                        counted.ground_only_hit = std::array<int32_t, 2>{x, y};
                } else {
                    ++counted.ground_only_move;
                }
            }
        return counted;
    };
    std::vector<std::string> failures;
    const auto report = [&](const std::string& what, const Samples& counted) {
        std::cout << "wreck pick check: " << what << ": over the wreck as drawn "
                  << counted.drawn_reclaim << " pointer positions show Reclaim, "
                  << counted.drawn_other << " do not; over where it would be drawn at the "
                  << "height the pointer meets " << counted.ground_only_move << " show Move, "
                  << counted.ground_only_reclaim << " Reclaim\n";
        if (counted.drawn_other != 0 || counted.ground_only_reclaim != 0 ||
            counted.drawn_reclaim == 0)
            failures.push_back(what);
    };
    // A left click at a pointer with the reclaimer selected must give it
    // the wreck's Reclaim.
    const auto click_reclaims = [&](
                                    uint16_t reclaimer, const WreckSite& wreck, int32_t x, int32_t y
                                ) {
        match_->stop_orders(reclaimer);
        match_command_ = MatchCommand::none;
        click(static_cast<float>(x), static_cast<float>(y));
        std::vector<oa::sim::match_runtime::Match::QueuedCommandView> queue;
        match_->visit_primary_queue(reclaimer, [&](const auto& order) { queue.push_back(order); });
        if (queue.empty() || queue.front().kind != kReclaimKind)
            return false;
        const auto column = (queue.front().destination[0] >> 20) - wreck.cell_x;
        const auto row = (queue.front().destination[2] >> 20) - wreck.cell_z;
        return column >= 0 && row >= 0 && column < wreck.footprint_x && row < wreck.footprint_z;
    };

    // The pointer over the part of a wreck's footprint drawn below where it
    // would be drawn at the sea's height, and over the part of that one
    // above the drawn footprint: the screen x and the two y.
    const auto sea_pointers = [&](const WreckSite& wreck) {
        const auto drawn = footprint_box(wreck, wreck.drawn_height);
        const auto surface = footprint_box(wreck, sea);
        return std::array<int32_t, 3>{
            (drawn[0] + drawn[2]) / 2,
            (std::max(surface[3], drawn[1]) + drawn[3]) / 2,
            (surface[1] + std::min(drawn[1], surface[3])) / 2
        };
    };

    // At sea: a construction ship beside each ship that is sunk.
    for (std::size_t index = 0; index < kSunkShips.size(); ++index) {
        const auto& site = ship_sites[index];
        std::string prefix = "wreck-pick-";
        for (const char* letter = kSunkShips[index]; *letter != '\0'; ++letter)
            prefix += static_cast<char>(std::tolower(static_cast<unsigned char>(*letter)));
        const auto x = site.x * OA_MAP_CELL_PIXELS + OA_MAP_CELL_PIXELS / 2;
        const auto z = site.z * OA_MAP_CELL_PIXELS + OA_MAP_CELL_PIXELS / 2;
        const auto ship = place(kSunkShips[index], x - kReclaimerApart / 2, z);
        const auto reclaimer = place("ARMCS", x + kReclaimerApart, z);
        for (int tick = 0; tick < 30; ++tick)
            step_match_simulation();
        auto wreck = destroy(ship);
        clear_local_selection();
        adopt_selection(reclaimer);
        std::cout << "wreck pick check: " << kSunkShips[index] << " destroyed over water "
                  << site.depth << " deep at cell " << site.x << "," << site.z << " left "
                  << wreck.name << " (" << wreck.footprint_x << "x" << wreck.footprint_z
                  << ") at cell " << wreck.cell_x << "," << wreck.cell_z << "\n";
        // While it sinks, half way to the bed.
        const auto bed = plot_height(wreck.cell_x, wreck.cell_z);
        bool sampled_sinking = false;
        int settled_tick = 0;
        for (int tick = 1; tick <= kSinkTicks && settled_tick == 0; ++tick) {
            step_match_simulation();
            const auto* record = wreck_record(wreck);
            if (record == nullptr)
                fail(wreck.name + " lost its record as it sank");
            wreck.drawn_height = record->model.position.y >> 16;
            if (!sampled_sinking && wreck.drawn_height <= (sea + bed) / 2) {
                centre_view(wreck);
                report(
                    wreck.name + " sinking at height " + std::to_string(wreck.drawn_height),
                    sample(wreck, sea)
                );
                const auto pointer = sea_pointers(wreck);
                std::cout << "wreck pick check: the pointer on " << wreck.name
                          << " where it is drawn as it sinks shows "
                          << cursor_name(hover(pointer[0], pointer[1])) << " (" << prefix
                          << "-sinking.png)\n";
                picture_with_cursor(prefix + "-sinking.png");
                sampled_sinking = true;
            }
            if (record->model.velocity.x == 0 && record->model.velocity.y == 0 &&
                record->model.velocity.z == 0)
                settled_tick = tick;
        }
        if (settled_tick == 0)
            fail(
                wreck.name + " did not come to rest within " + std::to_string(kSinkTicks) + " ticks"
            );
        std::cout << "wreck pick check: " << wreck.name << " came to rest on the sea bed at height "
                  << wreck.drawn_height << " by tick " << settled_tick << ", drawn "
                  << (sea - wreck.drawn_height) / 2 << " pixels below where it went down\n";
        centre_view(wreck);
        report(wreck.name + " on the sea bed", sample(wreck, sea));
        // The pictures: the pointer on the wreck as drawn, below where it
        // would be at the sea's height, and on the water above it.
        const auto pointer = sea_pointers(wreck);
        std::cout << "wreck pick check: the pointer on " << wreck.name
                  << " where it is drawn on the sea bed shows "
                  << cursor_name(hover(pointer[0], pointer[1])) << " (" << prefix
                  << "-sea-bed.png)\n";
        picture_with_cursor(prefix + "-sea-bed.png");
        std::cout << "wreck pick check: the pointer on the water above " << wreck.name
                  << ", where it went down, shows " << cursor_name(hover(pointer[0], pointer[2]))
                  << " (" << prefix << "-sea-surface.png)\n";
        picture_with_cursor(prefix + "-sea-surface.png");
        const bool reclaim_given = click_reclaims(reclaimer, wreck, pointer[0], pointer[1]);
        std::cout << "wreck pick check: a click on " << wreck.name << " where it is drawn gives "
                  << (reclaim_given ? "the construction ship its Reclaim"
                                    : "the construction ship no Reclaim of it")
                  << " (" << prefix << "-sea-order.png)\n";
        // The order's marker is drawn with the pointer off to the west.
        world.game.panel_unit_id = reclaimer;
        std::ignore = hover(pointer[0] - kOrderPictureApart, pointer[1]);
        picture_with_cursor(prefix + "-sea-order.png");
        if (!reclaim_given) {
            failures.push_back("a click on " + wreck.name + " where it is drawn");
        } else {
            const auto still_there = [&] {
                const auto* plot = oa::world_plot(&world, wreck.cell_x, wreck.cell_z);
                if (plot == nullptr || plot->feature >= world.feature_def_count)
                    return false;
                const auto& def = world.feature_defs[plot->feature];
                return std::string_view(def.name, ::strnlen(def.name, sizeof def.name)) ==
                       wreck.name;
            };
            int reclaimed_tick = 0;
            for (int tick = 1; tick <= kReclaimTicks && reclaimed_tick == 0; ++tick) {
                step_match_simulation();
                if (!still_there())
                    reclaimed_tick = tick;
            }
            if (reclaimed_tick == 0)
                failures.push_back(
                    "the construction ship did not reclaim " + wreck.name + " within " +
                    std::to_string(kReclaimTicks) + " ticks"
                );
            else
                std::cout << "wreck pick check: the construction ship reclaimed " << wreck.name
                          << " from the sea bed by tick " << reclaimed_tick << "\n";
        }
        match_->stop_orders(reclaimer);
    }

    // On the hill: a construction kbot beside a Stumpy that is destroyed.
    {
        const auto x = (*hill_site)[0] * OA_MAP_CELL_PIXELS + OA_MAP_CELL_PIXELS / 2;
        const auto z = (*hill_site)[1] * OA_MAP_CELL_PIXELS + OA_MAP_CELL_PIXELS / 2;
        const auto tank = place("ARMSTUMP", x - kReclaimerApart / 4, z);
        const auto reclaimer = place("ARMCK", x + kReclaimerApart / 2, z);
        for (int tick = 0; tick < 30; ++tick)
            step_match_simulation();
        auto wreck = destroy(tank);
        for (int tick = 0; tick < 60; ++tick)
            step_match_simulation();
        const auto* record = wreck_record(wreck);
        if (record == nullptr)
            fail(wreck.name + " has no record");
        wreck.drawn_height = record->model.position.y >> 16;
        clear_local_selection();
        adopt_selection(reclaimer);
        centre_view(wreck);
        std::cout << "wreck pick check: " << wreck.name << " (" << wreck.footprint_x << "x"
                  << wreck.footprint_z << ") at cell " << wreck.cell_x << "," << wreck.cell_z
                  << " lies on ground at height " << wreck.drawn_height << ", drawn "
                  << wreck.drawn_height / 2 << " pixels above where it would be at height 0\n";
        // On level ground the pointer meets the ground at the wreck's own
        // height, so the two footprints are one.
        report(wreck.name + " on high ground", sample(wreck, wreck.drawn_height));
        const auto drawn = footprint_box(wreck, wreck.drawn_height);
        const auto flat = footprint_box(wreck, 0);
        const auto middle_x = (drawn[0] + drawn[2]) / 2;
        const auto middle_y = (drawn[1] + drawn[3]) / 2;
        const auto flat_y = (flat[1] + flat[3]) / 2;
        std::cout << "wreck pick check: the pointer on " << wreck.name
                  << " where it is drawn shows " << cursor_name(hover(middle_x, middle_y))
                  << " (wreck-pick-armstump-hill.png)\n";
        picture_with_cursor("wreck-pick-armstump-hill.png");
        const auto flat_cursor = hover(middle_x, flat_y);
        std::cout << "wreck pick check: the pointer " << wreck.drawn_height / 2
                  << " pixels below it, where it would be drawn at height 0, shows "
                  << cursor_name(flat_cursor) << " (wreck-pick-armstump-height-0.png)\n";
        picture_with_cursor("wreck-pick-armstump-height-0.png");
        if (flat_cursor == input::OrderCursor::reclaim)
            failures.push_back("the pointer below " + wreck.name + " shows Reclaim");
        if (!click_reclaims(reclaimer, wreck, middle_x, middle_y))
            failures.push_back("a click on " + wreck.name + " where it is drawn");
        match_->stop_orders(reclaimer);
    }

    if (!failures.empty()) {
        std::string list;
        for (const auto& failure : failures)
            list += (list.empty() ? "" : "; ") + failure;
        fail("the pointer does not pick a wreck where it is drawn: " + list);
    }
    std::cout << "wreck pick check: the sunken and the hill wrecks are picked where they are "
                 "drawn\n";
}

} // namespace oa::app
