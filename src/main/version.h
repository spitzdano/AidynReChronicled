// The two playable versions (src/main/main.cpp): the 1.0 release and Aidyn Patch.
#pragma once

namespace aidyn {
    bool version_available(bool aidyn_patch);  // its ROM is stored
    void switch_version(bool aidyn_patch);     // remember it and restart into it (returns only if that fails)
}
