#include <cmath>
#include <cstdio>
#include "jfg_tools.h"

namespace jfg {
double RateMeter::sample(uint64_t count, double now) {
    if (!started) {
        started = true;
        last_count = count;
        last_time = now;
        return current;
    }
    if (now - last_time >= 0.25) {
        current = double(count - last_count) / (now - last_time);
        last_count = count;
        last_time = now;
    }
    return current;
}

std::vector<std::string> format_overlay(const OverlayData& d) {
    char line[160];
    std::vector<std::string> lines;
    std::snprintf(line, sizeof(line), "Game %.0f fps", d.game_fps);
    lines.emplace_back(line);
    if (d.render_fps > 0) std::snprintf(line, sizeof(line), "Render %.0f fps  %.1f ms", d.render_fps, 1000.0 / d.render_fps);
    else std::snprintf(line, sizeof(line), "Render %.0f fps", d.render_fps);
    lines.emplace_back(line);
    if (d.level >= 0) std::snprintf(line, sizeof(line), "Level %d  %s", d.level, d.level_name.c_str());
    else std::snprintf(line, sizeof(line), "Level -");
    lines.emplace_back(line);
    if (d.have_position) std::snprintf(line, sizeof(line), "Position %.0f, %.0f, %.0f", d.x, d.y, d.z);
    else std::snprintf(line, sizeof(line), "Position -");
    lines.emplace_back(line);
    std::snprintf(line, sizeof(line), "Audio %.0f ms", d.audio_ms);
    lines.emplace_back(line);
    return lines;
}

std::string exit_label(int index, float distance) {
    char text[64];
    std::snprintf(text, sizeof(text), "Exit %d (%.0f away)", index + 1, std::floor(distance));
    return text;
}
}
