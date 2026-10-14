#ifndef JFG_INPUT_SCRIPT_H
#define JFG_INPUT_SCRIPT_H
#include <cstdint>
#include <string>
#include <vector>

// Development aid: a scripted controller (JFG_INPUT_SCRIPT) so boot-to-gameplay checks need no hands on the keys.
// Script: entries "T+D BUTTONS [X Y [RX RY [MDX MDY]]]" separated by ';' — from T seconds for D seconds hold BUTTONS
// ('+'-joined names: A B Z START L R DU DD DL DR CU CD CL CR, or '-' for none), push the stick to X,Y (-1..1), the
// right stick to RX,RY (-1..1, modern look), and move the mouse MDX,MDY counts per game frame (modern look/aim).
namespace jfg {
struct InputEvent {
    double start, duration;
    uint16_t buttons;
    float x, y;
    float rx = 0, ry = 0;   // right stick
    float mdx = 0, mdy = 0; // mouse counts per game frame
};
// Throws std::invalid_argument on a malformed script (fail loudly: a typo shouldn't silently do nothing).
std::vector<InputEvent> parse_input_script(const std::string& script);
// Adds the input held at time t (seconds) to buttons/x/y.
void apply_input_script(const std::vector<InputEvent>& events, double t, uint16_t* buttons, float* x, float* y);
// Also adds the right stick (clamped to -1..1) and mouse counts.
void apply_input_script(const std::vector<InputEvent>& events, double t, uint16_t* buttons, float* x, float* y,
                        float* rx, float* ry, float* mdx, float* mdy);
}
#endif
