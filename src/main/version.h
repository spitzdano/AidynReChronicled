// The playable versions (src/main/main.cpp): the 1.0 release, Aidyn Patch and Aidyn Plus; and the window.
#pragma once

#include "settings.h"

namespace aidyn {
    bool version_available(settings::RomPatch version);  // its ROM is stored (the player's own, checked by its hash)
    void switch_version(settings::RomPatch version);     // remember it and restart into it (returns only if that fails)
    float window_aspect();                               // the window's width / height (0 before it exists)
    // Windowed or borderless, covering the screen (the Display page's Window row, F11 / Alt+Enter), remembered in
    // settings.json unless `save` is false.
    void set_borderless(bool on, bool save = true);
    // MSAA (0, 2, 4, 8) and supersampling (1 to 4), applied on the next frame and remembered in settings.json (the
    // Display page's Anti-aliasing row).
    void set_antialiasing(int msaa, int supersampling);
}
