#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

#include "librecomp/helpers.hpp"
#include "jfg_input_script.h"
#include "jfg_player_tools.h"
#include "../jfg/controls/settings.h"
#include "../jfg/controls/translate.h"
#include "../jfg/controls/input_state.h"
#include "recomp_input.h"
#include "ultramodern/ultramodern.hpp"

// Arrays that hold the mappings for every input for keyboard and controller respectively.
using input_mapping = std::array<recomp::InputField, recomp::bindings_per_input>;
using input_mapping_array = std::array<input_mapping, static_cast<size_t>(recomp::GameInput::COUNT)>;
static input_mapping_array keyboard_input_mappings{};
static input_mapping_array controller_input_mappings{};

// Make the button value array, which maps a button index to its bit field.
#define DEFINE_INPUT(name, value, readable) uint16_t(value##u),
static const std::array n64_button_values = {
    DEFINE_N64_BUTTON_INPUTS()
};
#undef DEFINE_INPUT

// Make the input name array.
#define DEFINE_INPUT(name, value, readable) readable,
static const std::vector<std::string> input_names = {
    DEFINE_ALL_INPUTS()
};
#undef DEFINE_INPUT

// Make the input enum name array.
#define DEFINE_INPUT(name, value, readable) #name,
static const std::vector<std::string> input_enum_names = {
    DEFINE_ALL_INPUTS()
};
#undef DEFINE_INPUT

size_t recomp::get_num_inputs() {
    return (size_t)GameInput::COUNT;
}

const std::string& recomp::get_input_name(GameInput input) {
    return input_names.at(static_cast<size_t>(input));
}

const std::string& recomp::get_input_enum_name(GameInput input) {
    return input_enum_names.at(static_cast<size_t>(input));
}

recomp::GameInput recomp::get_input_from_enum_name(const std::string_view enum_name) {
    auto find_it = std::find(input_enum_names.begin(), input_enum_names.end(), enum_name);
    if (find_it == input_enum_names.end()) {
        return recomp::GameInput::COUNT;
    }

    return static_cast<recomp::GameInput>(find_it - input_enum_names.begin());
}

// Due to an RmlUi limitation this can't be const. Ideally it would return a const reference or even just a straight up copy.
recomp::InputField& recomp::get_input_binding(GameInput input, size_t binding_index, recomp::InputDevice device) {
    input_mapping_array& device_mappings = (device == recomp::InputDevice::Controller) ?  controller_input_mappings : keyboard_input_mappings;
    input_mapping& cur_input_mapping = device_mappings.at(static_cast<size_t>(input));

    if (binding_index < cur_input_mapping.size()) {
        return cur_input_mapping[binding_index];
    }
    else {
        static recomp::InputField dummy_field = {};
        return dummy_field;
    }
}

void recomp::set_input_binding(recomp::GameInput input, size_t binding_index, recomp::InputDevice device, recomp::InputField value) {
    input_mapping_array& device_mappings = (device == recomp::InputDevice::Controller) ?  controller_input_mappings : keyboard_input_mappings;
    input_mapping& cur_input_mapping = device_mappings.at(static_cast<size_t>(input));

    if (binding_index < cur_input_mapping.size()) {
        cur_input_mapping[binding_index] = value;
    }
}

static bool input_held(recomp::GameInput input) {
    return recomp::get_input_digital(keyboard_input_mappings[(size_t)input]) ||
           recomp::get_input_digital(controller_input_mappings[(size_t)input]);
}

// The modern control scheme's actions as the N64 buttons the game reads (src/jfg/controls/translate.h).
static uint16_t modern_n64_buttons(bool* aim_out) {
    using recomp::GameInput;
    jfg::controls::Actions a;
    a.fire = input_held(GameInput::JFG_FIRE);
    a.aim = input_held(GameInput::JFG_AIM);
    a.jump = input_held(GameInput::JFG_JUMP);
    a.crouch = input_held(GameInput::JFG_CROUCH);
    a.weapon_prev = input_held(GameInput::JFG_WEAPON_PREV);
    a.weapon_next = input_held(GameInput::JFG_WEAPON_NEXT);
    a.quick[0] = input_held(GameInput::JFG_QUICK_1);
    a.quick[1] = input_held(GameInput::JFG_QUICK_2);
    a.quick[2] = input_held(GameInput::JFG_QUICK_3);
    a.quick[3] = input_held(GameInput::JFG_QUICK_4);
    a.c_left = input_held(GameInput::JFG_C_LEFT);
    a.c_right = input_held(GameInput::JFG_C_RIGHT);
    a.l = input_held(GameInput::JFG_L);
    a.start = input_held(GameInput::START);

    static bool aim_was_held = false;
    static bool aim_toggled = false;
    bool aim = a.aim;
    if (jfg::controls::settings().aim_toggle) {
        aim_toggled = jfg::controls::update_aim_toggle(a.aim && !aim_was_held, aim_toggled);
        aim = aim_toggled;
    } else {
        aim_toggled = false;
    }
    aim_was_held = a.aim;
    *aim_out = aim;
    return jfg::controls::to_n64_buttons(a, aim);
}

// The input script clock (JFG_INPUT_SCRIPT), in scripted seconds: wall seconds since the first controller read by
// default, or with JFG_INPUT_CLOCK=frames:<rate> the number of player 1 reads (one per game frame) divided by <rate>,
// so presses land on the same game state even when the machine is busy and the game falls behind the wall clock.
// count_read: this is a player 1 read (advances the frames clock).
static double script_time(bool count_read) {
    static const double frames_rate = []() {
        const char* v = std::getenv("JFG_INPUT_CLOCK");
        if (v == nullptr) return 0.0;
        const std::string clock(v);
        if (clock.rfind("frames:", 0) != 0) throw std::invalid_argument("JFG_INPUT_CLOCK: expected frames:<rate>, got '" + clock + "'");
        return std::stod(clock.substr(7));
    }();
    static const auto script_start = std::chrono::steady_clock::now();
    static std::atomic<uint64_t> reads = 0;
    const uint64_t n = count_read ? ++reads : reads.load();
    const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - script_start).count();
    static const bool debug_clock = std::getenv("JFG_DEBUG_INPUT_CLOCK") != nullptr;
    if (debug_clock && count_read && n % 120 == 0) {
        std::fprintf(stderr, "[input] %llu reads at %.2f s (%.1f reads/s)\n", (unsigned long long)n, wall, n / wall);
    }
    return frames_rate > 0.0 ? n / frames_rate : wall;
}

// Players 2-4: the N64 buttons and stick from that player's own gamepad through the controller bindings, when the
// port is connected (extra players on, or JFG_FAKE_PLAYERS). The Modern scheme is player 1's only.
// Development aid: JFG_INPUT_SCRIPT_P2 scripts player 2 like JFG_INPUT_SCRIPT, on the same clock.
static bool extra_player_input(int controller_num, uint16_t* buttons_out, float* x_out, float* y_out) {
    if (recomp::get_connected_device_info(controller_num).connected_device == ultramodern::input::Device::None) {
        return false;
    }
    recomp::InputPlayerScope player(controller_num);
    uint16_t buttons = 0;
    float x = 0.0f, y = 0.0f;
    if (!recomp::game_input_disabled()) {
        for (size_t i = 0; i < n64_button_values.size(); i++) {
            const size_t input_index = (size_t)recomp::GameInput::N64_BUTTON_START + i;
            buttons |= recomp::get_input_digital(controller_input_mappings[input_index]) ? n64_button_values[i] : 0;
        }
        x = recomp::get_input_analog(controller_input_mappings[(size_t)recomp::GameInput::X_AXIS_POS])
          - recomp::get_input_analog(controller_input_mappings[(size_t)recomp::GameInput::X_AXIS_NEG]);
        y = recomp::get_input_analog(controller_input_mappings[(size_t)recomp::GameInput::Y_AXIS_POS])
          - recomp::get_input_analog(controller_input_mappings[(size_t)recomp::GameInput::Y_AXIS_NEG]);
        recomp::apply_joystick_deadzone(x, y, &x, &y);
    }
    static const std::vector<jfg::InputEvent> p2_script = []() {
        const char* script = std::getenv("JFG_INPUT_SCRIPT_P2");
        return script != nullptr ? jfg::parse_input_script(script) : std::vector<jfg::InputEvent>{};
    }();
    if (controller_num == 1 && !p2_script.empty()) {
        jfg::apply_input_script(p2_script, script_time(false), &buttons, &x, &y);
    }
    *buttons_out = buttons;
    *x_out = std::clamp(x, -1.0f, 1.0f);
    *y_out = std::clamp(y, -1.0f, 1.0f);
    return true;
}

bool recomp::get_n64_input(int controller_num, uint16_t* buttons_out, float* x_out, float* y_out) {
    uint16_t cur_buttons = 0;
    float cur_x = 0.0f;
    float cur_y = 0.0f;
    
    if (controller_num != 0) {
        return extra_player_input(controller_num, buttons_out, x_out, y_out);
    }
    jfg::tools::count_game_frame();

    // Development aid: JFG_CONTROLS_SCHEME_FLIP_AT=<seconds> flips Modern/Original once, that long after the first
    // controller read, without saving (checks that a mid-game switch takes effect on the next frame).
    static const double scheme_flip_at = []() {
        const char* v = std::getenv("JFG_CONTROLS_SCHEME_FLIP_AT");
        return v != nullptr ? std::stod(v) : -1.0;
    }();
    if (scheme_flip_at >= 0.0) {
        static const auto first_read = std::chrono::steady_clock::now();
        static bool flipped = false;
        if (!flipped && std::chrono::duration<double>(std::chrono::steady_clock::now() - first_read).count() >= scheme_flip_at) {
            flipped = true;
            jfg::controls::settings().modern = !jfg::controls::settings().modern;
            std::fprintf(stderr, "[controls] scheme flipped to %s\n", jfg::controls::settings().modern ? "Modern" : "Original");
        }
    }
    const bool modern = jfg::controls::settings().modern;
    jfg::controls::ModernInput modern_in{};
    // Modern reads the right stick as look input itself, so it must not also act as the C-buttons.
    recomp::set_right_analog_suppressed(modern);

    if (!recomp::game_input_disabled()) {
        if (modern) {
            cur_buttons = modern_n64_buttons(&modern_in.aim_held);
            const float look_x = recomp::get_input_analog_unsuppressed(controller_input_mappings[(size_t)GameInput::JFG_LOOK_RIGHT])
                               + recomp::get_input_analog_unsuppressed(keyboard_input_mappings[(size_t)GameInput::JFG_LOOK_RIGHT])
                               - recomp::get_input_analog_unsuppressed(controller_input_mappings[(size_t)GameInput::JFG_LOOK_LEFT])
                               - recomp::get_input_analog_unsuppressed(keyboard_input_mappings[(size_t)GameInput::JFG_LOOK_LEFT]);
            const float look_y = recomp::get_input_analog_unsuppressed(controller_input_mappings[(size_t)GameInput::JFG_LOOK_UP])
                               + recomp::get_input_analog_unsuppressed(keyboard_input_mappings[(size_t)GameInput::JFG_LOOK_UP])
                               - recomp::get_input_analog_unsuppressed(controller_input_mappings[(size_t)GameInput::JFG_LOOK_DOWN])
                               - recomp::get_input_analog_unsuppressed(keyboard_input_mappings[(size_t)GameInput::JFG_LOOK_DOWN]);
            recomp::apply_joystick_deadzone(std::clamp(look_x, -1.0f, 1.0f), std::clamp(look_y, -1.0f, 1.0f),
                                            &modern_in.stick_look_x, &modern_in.stick_look_y);
            recomp::get_mouse_deltas_raw(&modern_in.mouse_dx, &modern_in.mouse_dy);
        } else {
            for (size_t i = 0; i < n64_button_values.size(); i++) {
                size_t input_index = (size_t)GameInput::N64_BUTTON_START + i;
                cur_buttons |= recomp::get_input_digital(keyboard_input_mappings[input_index]) ? n64_button_values[i] : 0;
                cur_buttons |= recomp::get_input_digital(controller_input_mappings[input_index]) ? n64_button_values[i] : 0;
            }
        }

        float joystick_deadzone = recomp::get_joystick_deadzone() / 100.0f;

        float joystick_x = recomp::get_input_analog(controller_input_mappings[(size_t)GameInput::X_AXIS_POS])
                        - recomp::get_input_analog(controller_input_mappings[(size_t)GameInput::X_AXIS_NEG]);

        float joystick_y = recomp::get_input_analog(controller_input_mappings[(size_t)GameInput::Y_AXIS_POS])
                        - recomp::get_input_analog(controller_input_mappings[(size_t)GameInput::Y_AXIS_NEG]);

        recomp::apply_joystick_deadzone(joystick_x, joystick_y, &joystick_x, &joystick_y);

        cur_x = recomp::get_input_analog(keyboard_input_mappings[(size_t)GameInput::X_AXIS_POS])
                - recomp::get_input_analog(keyboard_input_mappings[(size_t)GameInput::X_AXIS_NEG]) + joystick_x;

        cur_y = recomp::get_input_analog(keyboard_input_mappings[(size_t)GameInput::Y_AXIS_POS])
                - recomp::get_input_analog(keyboard_input_mappings[(size_t)GameInput::Y_AXIS_NEG]) + joystick_y;
    }

    // Development aid: JFG_INPUT_SCRIPT presses buttons at set times (see jfg_input_script.h).
    static const std::vector<jfg::InputEvent> input_script = []() {
        const char* script = std::getenv("JFG_INPUT_SCRIPT");
        return script != nullptr ? jfg::parse_input_script(script) : std::vector<jfg::InputEvent>{};
    }();
    // The clock runs from player 1's first read whether or not player 1 has a script, so player 2's can use it.
    const double t = script_time(true);
    if (!input_script.empty()) {
        jfg::apply_input_script(input_script, t, &cur_buttons, &cur_x, &cur_y, &modern_in.stick_look_x,
                                &modern_in.stick_look_y, &modern_in.mouse_dx, &modern_in.mouse_dy);
    }

    *buttons_out = cur_buttons;
    *x_out = std::clamp(cur_x, -1.0f, 1.0f);
    *y_out = std::clamp(cur_y, -1.0f, 1.0f);
    if (modern) {
        modern_in.move_x = *x_out;
        modern_in.move_y = *y_out;
        *buttons_out |= jfg::controls::aim_strafe_buttons(modern_in.aim_held, *x_out);
        jfg::controls::publish(modern_in);
        // The runtime reports +-1.0 as +-127; a real N64 stick tops out near +-80, which the game's tuning expects.
        *x_out *= 80.0f / 127.0f;
        *y_out *= 80.0f / 127.0f;
    }

    return true;
}
