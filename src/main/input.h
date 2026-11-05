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
    void set_stick_keys(bool on);           // the right stick's flicks are the port's (kept for take_menu_keys: left 0x20000, right 0x40000) instead of turning the pages; refresh every frame
    void press(uint32_t bits);              // the game is given these N64 button bits for about 90 ms, as if pressed (D-left 0x200, D-right 0x100)
    void set_menu_mode(bool on);            // the pause menu, a trainer or a shop: LT is C-left there, View nothing
    void set_card_mode(int mode);           // 1 field, 2 combat, 0 elsewhere: there View shows / hides the controls card
    bool quicksave_asked();                 // LB pressed in the field since last asked (src/game/patches.cpp)
    void set_dialogue(int state);           // a conversation: 0 none, 1 a line to move on from, 2 answers offered
    bool dialogue_skipping();               // View skipping the conversation (A pressed for the player, line by line)
    void set_menu_keys(uint32_t mask);      // the pause menu's own lists are up: those of C-down 4, C-up 8, D-up 0x800, D-down 0x400, L 0x20, R 0x10 are the port's (a mask with D-up / down takes the left stick's too); 0 = none; refresh every frame
    uint32_t take_menu_keys();              // their presses since the last call, as N64 button bits (C-down 4, C-up 8, D-up 0x800, D-down 0x400, L 0x20, R 0x10); a held up / down repeats
    bool controls_card();                   // the controls card shown (settings.json graphics.controls_card)
    bool keyboard_last();                   // the keyboard or mouse was used last (a pad's presses count for the pad): prompts show keys
    bool pad_b_held();                      // the pad's own B held (the Controls page: it cancels listening)
    bool start_held();                      // N64 Start held (Menu, keyboard Enter), as last polled
    void set_rumble(int controller_num, bool rumble);
    void set_controls_card(bool shown);     // the pause menu's ReChronicled page: show / hide it (and remember it)
    // The pause menu's ReChronicled page: while capturing, the game gets no buttons and a centred stick, and the page
    // reads them here instead (N64 button bits, sticks -1..1).
    void set_capture(bool on);
    void captured(uint32_t* buttons, float* x, float* y);
}
