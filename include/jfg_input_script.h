#ifndef JFG_INPUT_SCRIPT_H
#define JFG_INPUT_SCRIPT_H
#include <cstdint>
#include <string>
#include <vector>

// Development aid: a scripted controller (JFG_INPUT_SCRIPT) so boot-to-gameplay checks need no hands on the keys.
// Script: entries "T+D BUTTONS [X Y]" separated by ';' — from T seconds for D seconds hold BUTTONS ('+'-joined
// names: A B Z START L R DU DD DL DR CU CD CL CR, or '-' for none) and push the stick to X,Y (-1..1).
namespace jfg {
struct InputEvent {
    double start, duration;
    uint16_t buttons;
    float x, y;
};
// Throws std::invalid_argument on a malformed script (fail loudly: a typo shouldn't silently do nothing).
std::vector<InputEvent> parse_input_script(const std::string& script);
// Adds the input held at time t (seconds) to buttons/x/y.
void apply_input_script(const std::vector<InputEvent>& events, double t, uint16_t* buttons, float* x, float* y);
}
#endif
