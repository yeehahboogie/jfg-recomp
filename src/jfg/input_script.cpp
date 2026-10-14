#include "jfg_input_script.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace {
const std::unordered_map<std::string, uint16_t> button_bits = {
    {"A", 0x8000}, {"B", 0x4000}, {"Z", 0x2000}, {"START", 0x1000},
    {"DU", 0x0800}, {"DD", 0x0400}, {"DL", 0x0200}, {"DR", 0x0100},
    {"L", 0x0020}, {"R", 0x0010}, {"CU", 0x0008}, {"CD", 0x0004}, {"CL", 0x0002}, {"CR", 0x0001},
};

uint16_t parse_buttons(const std::string& names, const std::string& entry) {
    if (names == "-") {
        return 0;
    }
    uint16_t bits = 0;
    std::stringstream ss(names);
    std::string name;
    while (std::getline(ss, name, '+')) {
        auto it = button_bits.find(name);
        if (it == button_bits.end()) {
            throw std::invalid_argument("JFG_INPUT_SCRIPT: unknown button '" + name + "' in '" + entry + "'");
        }
        bits |= it->second;
    }
    return bits;
}
}

std::vector<jfg::InputEvent> jfg::parse_input_script(const std::string& script) {
    std::vector<InputEvent> events;
    std::stringstream entries(script);
    std::string entry;
    while (std::getline(entries, entry, ';')) {
        if (entry.find_first_not_of(" \t") == std::string::npos) {
            continue;
        }
        std::stringstream fields(entry);
        std::string when, buttons;
        InputEvent ev{0, 0, 0, 0, 0};
        if (!(fields >> when >> buttons)) {
            throw std::invalid_argument("JFG_INPUT_SCRIPT: expected 'T+D BUTTONS [X Y]' in '" + entry + "'");
        }
        size_t plus = when.find('+');
        try {
            if (plus == std::string::npos) throw std::invalid_argument("no +");
            ev.start = std::stod(when.substr(0, plus));
            ev.duration = std::stod(when.substr(plus + 1));
        } catch (const std::exception&) {
            throw std::invalid_argument("JFG_INPUT_SCRIPT: bad time 'T+D' in '" + entry + "'");
        }
        ev.buttons = parse_buttons(buttons, entry);
        if (fields >> ev.x) {
            if (!(fields >> ev.y)) {
                throw std::invalid_argument("JFG_INPUT_SCRIPT: stick needs X and Y in '" + entry + "'");
            }
        }
        events.push_back(ev);
    }
    return events;
}

void jfg::apply_input_script(const std::vector<InputEvent>& events, double t, uint16_t* buttons, float* x, float* y) {
    for (const InputEvent& ev : events) {
        if (t >= ev.start && t < ev.start + ev.duration) {
            *buttons |= ev.buttons;
            *x = std::clamp(*x + ev.x, -1.0f, 1.0f);
            *y = std::clamp(*y + ev.y, -1.0f, 1.0f);
        }
    }
}
