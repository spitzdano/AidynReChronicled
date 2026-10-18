// The two playable versions (src/main/main.cpp): the 1.0 release and Aidyn Patch; and the window's shape.
#pragma once

namespace aidyn {
    bool version_available(bool aidyn_patch);  // its ROM is stored
    void switch_version(bool aidyn_patch);     // remember it and restart into it (returns only if that fails)
    float window_aspect();                     // the window's width / height (0 before it exists)
}
