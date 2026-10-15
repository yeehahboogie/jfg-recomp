#include "jfg_level_tools.h"

#include <stdexcept>

namespace {
int parse_number(const std::string& s, const std::string& whole) {
    size_t used = 0;
    long v = -1;
    try {
        v = std::stol(s, &used, 0);
    } catch (const std::exception&) {
        used = 0;
    }
    if (s.empty() || used != s.size() || v < 0 || v > 0xFFFF) {
        throw std::invalid_argument("JFG_WARP: expected <level>[:<entry>] (numbers), got '" + whole + "'");
    }
    return (int)v;
}
}

jfg::WarpSpec jfg::parse_warp(const std::string& text) {
    const size_t colon = text.find(':');
    if (colon == std::string::npos) {
        return {parse_number(text, text), 0};
    }
    return {parse_number(text.substr(0, colon), text), parse_number(text.substr(colon + 1), text)};
}
