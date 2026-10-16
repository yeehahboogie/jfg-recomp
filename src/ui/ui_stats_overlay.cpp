// The stats overlay (F3, or General -> Stats Overlay): game and render frame rates, frame time, the current level,
// player 1's position and the audio latency, in a corner panel that never takes input.
#include <chrono>
#include <string>
#include <vector>

#include "RmlUi/Core.h"
#include "core/ui_context.h"
#include "recomp_ui.h"
#include "zelda_config.h"
#include "zelda_support.h"
#include "ultramodern/ultramodern.hpp"
#include "jfg_player_tools.h"
#include "jfg_tools.h"
#include "ui_stats_overlay.h"
#include "../jfg/controls/settings.h"

namespace {
recompui::ContextId overlay_context = recompui::ContextId::null();
Rml::DataModelHandle model_handle;
std::vector<std::string> stats_lines;
bool shown = false;
}

void recompui::make_stats_overlay_bindings(Rml::Context* context) {
    Rml::DataModelConstructor constructor = context->CreateDataModel("stats_model");
    if (!constructor) {
        throw std::runtime_error("Failed to make RmlUi data model for the stats overlay");
    }
    constructor.RegisterArray<std::vector<std::string>>();
    constructor.Bind("stats_lines", &stats_lines);
    model_handle = constructor.GetModelHandle();
}

void recompui::load_stats_overlay() {
    overlay_context = recompui::create_context(zelda64::get_asset_path("stats_overlay.rml"));
    overlay_context.set_captures_input(false);
    overlay_context.set_captures_mouse(false);
}

// Called at the start of every UI frame, before the UI lock is taken (it may show or hide the overlay).
void recompui::update_stats_overlay() {
    if (overlay_context == recompui::ContextId::null()) return;
    // Only once the game runs: before that the launcher must be the only context.
    const bool want = jfg::controls::settings().show_stats && ultramodern::is_game_started();
    if (want != shown || (want && !recompui::is_context_shown(overlay_context))) {
        if (want && !recompui::is_context_shown(overlay_context)) recompui::show_context(overlay_context, "");
        if (!want && recompui::is_context_shown(overlay_context)) recompui::hide_context(overlay_context);
        shown = want;
    }
    if (!want) return;

    static jfg::RateMeter game_meter, render_meter;
    static auto last_update = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    const double t = std::chrono::duration<double>(now.time_since_epoch()).count();
    const double game_fps = game_meter.sample(jfg::tools::game_frames(), t);
    const double render_fps = render_meter.sample(jfg::tools::render_frames(), t);
    if (now - last_update < std::chrono::milliseconds(250)) return;
    last_update = now;

    const jfg::tools::GameInfo info = jfg::tools::game_info();
    jfg::OverlayData data;
    data.game_fps = game_fps;
    data.render_fps = render_fps;
    if (info.in_gameplay) {
        data.level = info.level;
        const std::vector<std::string> names = jfg::tools::level_names();
        if (info.level >= 0 && info.level < (int)names.size()) data.level_name = names[info.level];
        data.have_position = true;
        data.x = info.x; data.y = info.y; data.z = info.z;
    }
    data.audio_ms = jfg::tools::audio_queued_ms();
    stats_lines = jfg::format_overlay(data);
    model_handle.DirtyVariable("stats_lines");
}
