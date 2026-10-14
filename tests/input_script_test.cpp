#include <cassert>
#include <cstdio>
#include <stdexcept>
#include "jfg_input_script.h"

int main() {
    auto ev = jfg::parse_input_script("20+0.2 START; 24.5+0.5 A+B ; 30+3 - 0 1");
    assert(ev.size() == 3);
    uint16_t b = 0; float x = 0, y = 0;
    jfg::apply_input_script(ev, 19.9, &b, &x, &y); assert(b == 0 && x == 0 && y == 0);
    jfg::apply_input_script(ev, 20.1, &b, &x, &y); assert(b == 0x1000);
    b = 0; jfg::apply_input_script(ev, 20.3, &b, &x, &y); assert(b == 0);
    b = 0; jfg::apply_input_script(ev, 24.6, &b, &x, &y); assert(b == (0x8000 | 0x4000));
    b = 0; jfg::apply_input_script(ev, 31, &b, &x, &y); assert(b == 0 && x == 0 && y == 1.0f);
    // Look axes: "T+D BUTTONS X Y RX RY MDX MDY" (right stick, mouse counts per frame).
    auto look = jfg::parse_input_script("5+1 - 0 0 0.5 -0.5 10 0; 7+1 R 0 0 1 0");
    float rx = 0, ry = 0, mdx = 0, mdy = 0;
    b = 0; x = 0; y = 0;
    jfg::apply_input_script(look, 5.5, &b, &x, &y, &rx, &ry, &mdx, &mdy);
    assert(b == 0 && x == 0 && rx == 0.5f && ry == -0.5f && mdx == 10 && mdy == 0);
    rx = ry = mdx = mdy = 0;
    jfg::apply_input_script(look, 7.5, &b, &x, &y, &rx, &ry, &mdx, &mdy);
    assert(b == 0x0010 && rx == 1.0f && ry == 0 && mdx == 0);
    // The old overload ignores the look axes.
    b = 0; x = 0; y = 0;
    jfg::apply_input_script(look, 5.5, &b, &x, &y);
    assert(b == 0 && x == 0 && y == 0);
    bool look_threw = false;
    try { jfg::parse_input_script("5+1 - 0 0 0.5"); } catch (const std::invalid_argument&) { look_threw = true; }
    assert(look_threw);
    bool threw = false;
    try { jfg::parse_input_script("20+0.2 STRAT"); } catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
    std::puts("input_script_test: OK");
}
