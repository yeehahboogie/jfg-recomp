#ifndef JFG_OVERLAYS_HPP
#define JFG_OVERLAYS_HPP

namespace zelda64 {
    // Registers the recompiled code sections (main segment + JFG's code modules) with the runtime.
    void register_overlays();
}

#endif
