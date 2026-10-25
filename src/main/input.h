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
    bool c_down_held();                     // N64 C-down held (X, keyboard K), as last polled (title screen: switch version)
    bool c_up_held();                       // N64 C-up held (Y, keyboard I), as last polled (title screen: switch version)
    void set_menu_mode(bool on);            // the pause menu, a trainer or a shop: LT is C-left there, View nothing
    void set_card_mode(int mode);           // 1 field, 2 combat, 0 elsewhere: there LB shows / hides the controls card
    bool quicksave_asked();                 // View pressed in the field since last asked (src/game/patches.cpp)
    bool controls_card();                   // the controls card shown (settings.json graphics.controls_card)
    bool start_held();                      // N64 Start held (Menu, keyboard Enter), as last polled
    void set_rumble(int controller_num, bool rumble);
    void set_controls_card(bool shown);     // the pause menu's ReChronicled page: show / hide it (and remember it)
    // The pause menu's ReChronicled page: while capturing, the game gets no buttons and a centred stick, and the page
    // reads them here instead (N64 button bits, sticks -1..1).
    void set_capture(bool on);
    void captured(uint32_t* buttons, float* x, float* y);
}
