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
    bool threw = false;
    try { jfg::parse_input_script("20+0.2 STRAT"); } catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
    std::puts("input_script_test: OK");
}
