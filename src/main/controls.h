// Keyboard and mouse controls (src/main/controls.cpp): what each action of the game is bound to, two inputs each, kept
// in settings.json's "controls" section; the mouse as the camera's right stick. The pad is fixed (input.cpp).
#pragma once
#include <cstdint>
#include <string>

union SDL_Event;

namespace aidyn::controls {
    // The game's actions, named after the Xbox pad's buttons (the prompts show those icons): the left stick's four
    // directions, the D-pad's, A B X Y, View, the triggers and bumpers, Menu, the camera (the right stick's four
    // directions and its click) and fast-forward.
    enum Action : int {
        kMoveUp, kMoveDown, kMoveLeft, kMoveRight,
        kDpadUp, kDpadDown, kDpadLeft, kDpadRight,
        kA, kB, kX, kY, kView, kLT, kRT, kLB, kRB, kMenu,
        kCamUp, kCamDown, kCamLeft, kCamRight, kCamReset,
        kFastForward,
        kActionCount
    };
    constexpr int kSlots = 2;

    enum Kind : uint8_t { kNone = 0, kKey = 1, kMouse = 2, kWheel = 3 };
    // A key (code: SDL's scancode), a mouse button (code: SDL's button 1 left, 2 middle, 3 right, 4 back, 5 forward) or
    // a wheel direction (code 0 up, 1 down).
    struct Input {
        uint8_t kind = kNone;
        uint16_t code = 0;
        bool empty() const { return kind == kNone; }
        bool operator==(const Input& o) const { return kind == o.kind && code == o.code; }
    };

    struct ActionInfo {
        const char* id;     // in settings.json
        const char* label;  // the Controls page's row
    };
    const ActionInfo& info(Action a);

    Input binding(Action a, int slot);
    // Binds an input to an action's slot. An input is used once: another action that had it loses it (returned, or -1),
    // and so does the other slot of this one. Saved into settings.json.
    int set_binding(Action a, int slot, Input in);
    void clear_binding(Action a, int slot);
    void reset_defaults();                      // every binding and the mouse settings, saved
    bool is_escape(Input in);                   // Esc: cancels listening
    bool reserved(Input in);                    // keys the program keeps for itself (F1, F2, F11) and Esc
    std::string input_name(Input in);           // "Z", "Mouse Left", "Wheel Up"
    std::string action_keys(Action a);          // "Z / Mouse Left", or "-"

    bool down(Action a);                        // held, through either slot (keyboard, mouse, AIDYN_INPUT_FILE)

    // The mouse as the camera. Sensitivity steps of degrees a pixel; invert for the vertical look.
    bool mouse_look();
    void set_mouse_look(bool on);
    float mouse_sensitivity();
    void set_mouse_sensitivity(float s);
    bool mouse_invert_y();
    void set_mouse_invert_y(bool on);
    // Degrees the mouse has turned (right positive) and tilted (up positive) since the last call, as the camera asks
    // once per game tick; nothing while the camera isn't asking (menus, fights, conversations).
    void look(float* turn_degrees, float* tilt_degrees);
    // The field wants the mouse captured (hidden, unlimited); applied on the main thread by apply_mouse_mode.
    void want_capture(bool on);
    void apply_mouse_mode();
    // The pointer over the menus, in their 320 x 240 units (the picture's middle 4:3); moves counts its movements (a
    // page follows it only when it has moved). False before it has moved over the window.
    bool pointer(float* x, float* y, uint32_t* moves);

    // Events from the window (main thread): the mouse's motion, buttons and wheel, and key presses for listening.
    void handle_event(const SDL_Event& event);
    // The Controls page binds by listening: the next key, mouse button or wheel step, once.
    void listen(bool on);
    bool take_pressed(Input* in);
}
