#include <array>
#include <chrono>
#include <cstdlib>
#include <vector>

#include "librecomp/helpers.hpp"
#include "jfg_input_script.h"
#include "../jfg/controls/settings.h"
#include "../jfg/controls/translate.h"
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
static uint16_t modern_n64_buttons() {
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
    return jfg::controls::to_n64_buttons(a, aim);
}

bool recomp::get_n64_input(int controller_num, uint16_t* buttons_out, float* x_out, float* y_out) {
    uint16_t cur_buttons = 0;
    float cur_x = 0.0f;
    float cur_y = 0.0f;
    
    if (controller_num != 0) {
        return false;
    }

    const bool modern = jfg::controls::settings().modern;
    // Modern reads the right stick as look input itself, so it must not also act as the C-buttons.
    recomp::set_right_analog_suppressed(modern);

    if (!recomp::game_input_disabled()) {
        if (modern) {
            cur_buttons = modern_n64_buttons();
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
    if (!input_script.empty()) {
        static const auto script_start = std::chrono::steady_clock::now();
        double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - script_start).count();
        jfg::apply_input_script(input_script, t, &cur_buttons, &cur_x, &cur_y);
    }

    *buttons_out = cur_buttons;
    *x_out = std::clamp(cur_x, -1.0f, 1.0f);
    *y_out = std::clamp(cur_y, -1.0f, 1.0f);
    if (modern) {
        // The runtime reports +-1.0 as +-127; a real N64 stick tops out near +-80, which the game's tuning expects.
        *x_out *= 80.0f / 127.0f;
        *y_out *= 80.0f / 127.0f;
    }

    return true;
}
