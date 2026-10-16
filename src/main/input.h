#pragma once
#include <cstdint>

union SDL_Event;

namespace aidyn::input {
    void handle_event(const SDL_Event& event);
    void poll();
    bool get(int controller_num, uint16_t* buttons, float* x, float* y);
    void right_stick(float* x, float* y); // -1..1 after the dead zone, as last polled; y up = positive
    bool right_stick_click();               // R3 held, as last polled (recentres the camera)
    bool fast_forward();                    // fast-forward on: toggled by L3, or Tab held (src/game/patches.cpp)
    bool c_left_held();                     // N64 C-left held (View, keyboard J), as last polled (title screen: switch version)
    void set_rumble(int controller_num, bool rumble);
}
