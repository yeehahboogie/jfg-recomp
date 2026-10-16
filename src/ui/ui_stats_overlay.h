#ifndef UI_STATS_OVERLAY_H
#define UI_STATS_OVERLAY_H
namespace Rml { class Context; }
namespace recompui {
// The stats overlay (src/ui/ui_stats_overlay.cpp): its data model, its document, and the per-frame update that shows
// or hides it from the saved setting and refreshes its numbers four times a second.
void make_stats_overlay_bindings(Rml::Context* context);
void load_stats_overlay();
void update_stats_overlay();
// The Cheats tab's per-frame refresh (src/ui/ui_config.cpp).
void update_cheats_menu();
}
#endif
