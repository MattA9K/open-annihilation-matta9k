// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-mod-map-settings: a mod's settings for a map the game does not
// have. The run plays a mod folder (--mod-dir) whose loose maps folder holds
// an OTA with no terrain for a map the check writes as a pack, one for no map
// at all, and one with junk terrain and no start positions. The check
// requires the two maps with no terrain out of the lists, the pack map listed
// and mounted with the mod's OTA over the pack's terrain, and a listed map
// that does not load shown in the picker with the game's own message and no
// picture.

#include "map_pack_check_packs.hpp"
#include "map_packs.hpp"
#include "map_picture_state.hpp"

#include "oa/app/game_directory.hpp"
#include "oa/app/pack_map_source.hpp"
#include "oa/app/package_install/oamap.hpp"
#include "oa/app/runtime.hpp"
#include "oa/data/map_pack/map_name.hpp"
#include "oa/ui/frontend_multiplayer/screens.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace oa::app {

namespace {

namespace pack = oa::data::map_pack;

/// The pack the check writes, which the mod ships settings for.
constexpr std::string_view kSettledPack = "oa-check-settled";
/// That pack's name.
constexpr std::string_view kSettledPackName = "Mod Map Settings Check";
/// Its map's stem: the mod's OTA of the same name has no terrain.
constexpr std::string_view kSettledStem = "check_settled";
/// The title the pack's index gives the map.
constexpr std::string_view kPackTitle = "Check Settled";

/// The mod's settings for the pack map, as the run's mod folder holds them:
/// its mission name, description, size and player counts.
constexpr std::string_view kModTitle = "Settled by the Mod";
constexpr std::string_view kModDescription = "The mod's own settings for this map.";
constexpr std::string_view kModSize = "3 x 3";

/// The mod's OTA for no map at all: it has no terrain and no pack.
constexpr std::string_view kLonelyStem = "check_lonely";

/// The mod's map with junk terrain and a schema with no start positions: the
/// list shows it, and it does not load.
constexpr std::string_view kUnreadyStem = "check_unready";

/// Throws when a step of the check fails.
///
/// @param ok the step passed
/// @param what what failed
void require(bool ok, std::string_view what) {
    if (!ok)
        throw std::runtime_error("mod map settings check: " + std::string(what));
}

/// Reads a file whole.
///
/// @param file the file
/// @return its bytes; empty when it cannot be read
std::vector<uint8_t> read_whole(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

/// Returns the text of a label or a button.
///
/// @param gadget the gadget, or null
/// @return its text; empty for another gadget or none
std::string gadget_text(const oa::ui::gui_layout::Gadget* gadget) {
    if (gadget == nullptr)
        return {};
    if (const auto* label = std::get_if<oa::ui::gui_layout::LabelFields>(&gadget->fields))
        return label->text;
    if (const auto* button = std::get_if<oa::ui::gui_layout::ButtonFields>(&gadget->fields))
        return button->text;
    return {};
}

} // namespace

void Runtime::check_mod_map_settings() {
    require(plays_mod(), "the run plays no mod folder (--mod-dir)");
    require(
        options_.user_folder && !options_.user_folder->empty() && !user_folder_.empty(),
        "the run has no folder of the player's own (--user-folder)"
    );
    const std::filesystem::path mod_maps = played_mod_folder() / "maps";
    const std::vector<uint8_t> mod_settings =
        read_whole(mod_maps / (std::string(kSettledStem) + ".ota"));
    require(!mod_settings.empty(), "the mod folder holds no settings for the pack map");

    // The pack map's rock is drawn with a model of the game.
    const auto models = assets_.list_effective("objects3d", ".3do", false);
    require(!models.empty(), "the game has no model for the check's feature");
    const std::string model = path_to_utf8(path_from_utf8(models.front()).stem());
    const std::filesystem::path maps =
        user_folder_ / std::string(package_install::oamap::maps_folder_name);
    const MapPackCheckMap settled_map{kSettledStem, kPackTitle, false};
    write_map_pack_check_folder(
        maps / std::string(kSettledPack), kSettledPack, kSettledPackName, {&settled_map, 1}, model
    );
    map_packs().refresh();
    const std::string settled = pack::pack_map_name(kSettledStem, kSettledPack);
    const PackMap* installed = map_packs().find(settled);
    require(installed != nullptr, "the pack written is not installed");

    // The mod's maps with no terrain are out of the lists; the one whose
    // terrain is there stays. The pack map's settings are kept.
    eligible_map_names_.clear();
    first_map_name_.clear();
    discover_first_map();
    const auto listed = [this](std::string_view name) {
        return std::find(eligible_map_names_.begin(), eligible_map_names_.end(), name) !=
               eligible_map_names_.end();
    };
    const auto summarised = [this](std::string_view name) {
        return std::any_of(
            base_map_summaries_.begin(),
            base_map_summaries_.end(),
            [name](const BaseMapSummary& summary) { return summary.name == name; }
        );
    };
    require(
        !listed(kSettledStem) && !listed(kLonelyStem) && !summarised(kSettledStem) &&
            !summarised(kLonelyStem),
        "a map whose terrain is missing is listed"
    );
    require(
        listed(kUnreadyStem) && summarised(kUnreadyStem) && listed(settled),
        "the map whose terrain is there, or the pack map, is not listed"
    );
    require(
        mod_map_settings(kSettledStem) != nullptr && mod_map_settings(kLonelyStem) != nullptr &&
            mod_map_settings(kUnreadyStem) == nullptr,
        "the mod's settings are not found for exactly its maps with no terrain"
    );
    installed = map_packs().find(settled);
    require(installed != nullptr, "the pack map is gone");
    const BaseMapSummary* settings = pack_map_mod_summary(*installed);
    require(
        settings != nullptr && settings->title == kModTitle &&
            settings->description == kModDescription && settings->size == kModSize &&
            settings->player_counts == std::vector<int32_t>{2, 4} && settings->players == 4,
        "the pack map does not take the mod's title, description, size and player counts"
    );
    require(
        pack_map_title(*installed) == kModTitle && pack_map_shown_name(settled) == kModTitle,
        "the pack map is not shown under the mod's title alone"
    );
    std::cout << "mod map settings check: " << kSettledStem << " and " << kLonelyStem
              << " left out; " << settled << " takes the mod's settings\n";

    // The battle room's list shows the mod's values for the pack map.
    const oa::ui::frontend_multiplayer::LobbyMapSource source = pack_map_source(*this);
    bool lobby_row = false;
    for (int32_t index = 0; index < source.count(source.context); ++index) {
        oa::ui::frontend_multiplayer::LobbyPackMap row{};
        if (!source.at(source.context, index, &row) || row.name == nullptr ||
            std::string_view(row.name) != settled)
            continue;
        lobby_row = row.title != nullptr && std::string_view(row.title) == kModTitle &&
                    row.description != nullptr &&
                    std::string_view(row.description) == kModDescription && row.size != nullptr &&
                    std::string_view(row.size) == kModSize;
    }
    require(lobby_row, "the battle room's list does not show the mod's values for the pack map");

    // Choosing the pack map mounts the mod's OTA over the pack's terrain.
    const std::string ota = "maps/" + settled + ".ota";
    const std::string terrain = "maps/" + settled + ".tnt";
    require(pack_map_fit(*installed).fits(), "the pack map does not fit with the mod's settings");
    require(select_map(settled) == 1, "the pack map with the mod's settings was not selected");
    require(
        read(ota) == mod_settings && read(terrain).has_value() &&
            map_description() == kModDescription,
        "the mounted pack map does not read the mod's OTA over its own terrain"
    );
    state_.player_count = 2;
    require(map_player_capacity() >= 2, "the mod's settings lack two start positions");
    std::cout << "mod map settings check: " << ota << " is the mod's file\n";

    // In the picker the pack map's row is the mod's title, and it previews
    // with the mod's description.
    const auto row_of = [this](std::string_view name) {
        return static_cast<std::size_t>(
            std::find(bound_map_names_.begin(), bound_map_names_.end(), name) -
            bound_map_names_.begin()
        );
    };
    exercise_click(menu::resource_name(menu::Button::single_player));
    exercise_click(entry::resource_name(entry::Button::skirmish));
    require(screen_ == Screen::skirmish, "Skirmish did not open its setup");
    exercise_click(skirmish::resource_name(skirmish::Button::select_map));
    require(
        screen_ == Screen::map_selection && row_of(settled) < bound_map_labels_.size() &&
            bound_map_labels_[row_of(settled)] == kModTitle &&
            row_of(kUnreadyStem) < bound_map_names_.size(),
        "the map picker does not list the pack map under the mod's title alone"
    );
    preview_map_index(row_of(settled));
    require(
        map_picture_ && !map_picture_->rgb.empty() &&
            gadget_text(widget("DESCRIPTION")) == kModDescription,
        "the map picker does not preview the pack map with the mod's description"
    );

    // A listed map that does not load shows its name and the game's own
    // message, and not the map highlighted before it.
    const std::string missing = translate_ui(entry::message_text(entry::Message::missing_terrain));
    preview_map_index(row_of(kUnreadyStem));
    const auto* title = widget("MAPNAME");
    require(
        (title == nullptr || gadget_text(title) == kUnreadyStem) &&
            gadget_text(widget("DESCRIPTION")) == missing &&
            (!map_picture_ || map_picture_->rgb.empty()) &&
            map_refusal(kUnreadyStem) ==
                std::string(entry::message_text(entry::Message::missing_terrain)),
        "a map that does not load still shows the map highlighted before it"
    );
    close_map_modal();
    std::cout << "mod map settings check: " << kUnreadyStem << " shows \"" << missing << "\"\n";

    load(Screen::main_menu);
    require(!assets_.pack_layer_mounted(), "the main menu shows a pack map's files");
    std::cout << "mod map settings check passed\n";
}

} // namespace oa::app
