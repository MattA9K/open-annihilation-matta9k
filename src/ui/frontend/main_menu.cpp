// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// MAINMENU.GUI setup and its once-only start-up checks.
#include "oa/ui/frontend/main_menu.hpp"

#include <cstdint>
#include <string>
#include <string_view>

#ifndef OA_ENGINE_VERSION
#error "OA_ENGINE_VERSION names this build's version, which the main menu's version label shows"
#endif
#ifndef OA_ENGINE_VERSION_LABEL
#error "OA_ENGINE_VERSION_LABEL names this build's version label, empty for a release"
#endif

namespace oa::ui::frontend {

std::string main_menu_version_text() {
    // The same version the Library's header writes as "v" plus
    // engine_version_text of OA_ENGINE_VERSION. A release version is
    // major.minor.patch, which that text prints unchanged.
    std::string text(kMainMenuProgramName);
    text += " v";
    text += OA_ENGINE_VERSION;
    constexpr std::string_view label = OA_ENGINE_VERSION_LABEL;
    if (!label.empty()) {
        text.push_back('-');
        text.append(label);
    }
    return text;
}

std::string main_menu_unbound_text() {
    std::string text(kMainMenuUnboundName);
    text += " v";
    text += kMainMenuUnboundVersion;
    return text;
}

namespace {

const char* translate(const MainMenuHost& host, const char* text) {
    if (host.translate == nullptr)
        return text;
    const char* replacement = host.translate(host.context, text);
    return replacement != nullptr ? replacement : text;
}

// Shows the program's version, Open Annihilation: Unbound's line under it
// after a CR, and a mod's display version under those after another, and
// centres the first line on the label's authored x.
void place_version_label(Panel& panel, const MainMenuHost& host) {
    panel_set_active(panel, kMainMenuVersionControl, 1);
    auto* label = panel_control(panel, kMainMenuVersionControl);
    if (label == nullptr)
        return;
    // The program's name and a mod's display version are shown as given.
    const std::string first_line = main_menu_version_text();
    std::string text = first_line + '\r' + main_menu_unbound_text();
    if (host.version_text != nullptr && host.version_text[0] != '\0') {
        text.push_back('\r');
        text += host.version_text;
    }
    set_control_text(*label, text);
    panel.dirty = true;
    const int32_t width =
        host.measure_text != nullptr ? host.measure_text(host.context, first_line.c_str()) : 0;
    label->x = static_cast<int16_t>(label->x + width / -2);
}

} // namespace

void main_menu_setup(Panel& panel, MainMenuChecks& checks, const MainMenuHost& host) noexcept {
    if (host.play_music != nullptr)
        host.play_music(host.context, kMainMenuMusic);
    if (host.set_music_kind != nullptr)
        host.set_music_kind(host.context, kMainMenuMusicKind);
    if (host.revision_named == nullptr || host.revision_named(host.context))
        place_version_label(panel, host);
    if (host.movies_present != nullptr && !host.movies_present(host.context)) {
        panel_set_grayed(panel, kMainMenuIntroControl, true);
        panel_set_active(panel, kMainMenuCreditsControl, 0);
    }
    if (host.reset_sparks != nullptr)
        host.reset_sparks(host.context);
    if (!checks.cd_player_checked && host.foreign_cd_player != nullptr &&
        host.foreign_cd_player(host.context)) {
        if (host.close_cd_player != nullptr)
            host.close_cd_player(host.context);
        checks.cd_player_checked = true;
    }
    if (!checks.sound_driver_checked && host.sound_driver_missing != nullptr &&
        host.sound_driver_missing(host.context)) {
        if (host.show_message != nullptr)
            host.show_message(
                host.context, translate(host, kNoSoundDriverMessage), kSoundDriverWarningWidth
            );
        checks.sound_driver_checked = true;
    }
    if (!checks.revision_checked) {
        if (host.check_revision != nullptr)
            host.check_revision(host.context);
        checks.revision_checked = true;
    }
}

} // namespace oa::ui::frontend
