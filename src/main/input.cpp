#include <atomic>
// Minimal input: the first SDL game controller, with the keyboard as a fallback so the game can
// be tested without a pad. Everything feeds N64 port 1.
//
// Pad:       A/B/X/Y -> A/B/Z/(unused)   right stick -> C buttons   triggers -> L/R, Z
//            left stick, D-pad, Start as expected; right shoulder -> R, left shoulder -> L
//            (in the field and in combat LB shows / hides the controls card, and in the field View quicksaves: get())
// Keyboard:  arrows = stick, Z = A, X = B, C = Z, Enter = Start, WASD-less on purpose
//            IJKL = C buttons, Q = L, E = R, 8456 = D-pad
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>

#include "SDL2/SDL.h"

#include "input.h"
#include "settings.h"
#include "rt64_context.h"

namespace {
    enum N64Button : uint16_t {
        A = 0x8000, B = 0x4000, Z = 0x2000, START = 0x1000,
        DU = 0x0800, DD = 0x0400, DL = 0x0200, DR = 0x0100,
        L = 0x0020, R = 0x0010,
        CU = 0x0008, CD = 0x0004, CL = 0x0002, CR = 0x0001,
    };

    SDL_GameController* pad = nullptr;
    std::mutex pad_mutex;
    constexpr float stick_deadzone = 0.15f;

    bool scanned = false;
    // The right stick as last polled, for the camera (src/game/patches.cpp).
    std::atomic<float> right_x{ 0.0f }, right_y{ 0.0f };
    std::atomic_bool right_click{ false };
    std::atomic_bool ff_toggled{ false }, ff_held{ false };
    bool l3_was_down = false;
    std::atomic_bool c_down{ false }, c_up{ false };
    std::atomic_bool capture{ false };
    std::atomic_uint32_t captured_buttons{ 0 };
    std::atomic<float> captured_x{ 0.0f }, captured_y{ 0.0f };
    std::atomic_int card_mode{ 0 };   // aidyn::input::set_card_mode
    std::atomic_bool start{ false };
    std::atomic_bool quicksave_pressed{ false };  // View pressed in the field: aidyn::input::quicksave_asked
    std::atomic_int card_state{ -1 }; // the controls card: -1 as settings.json says, else shown (1) or hidden (0)

    bool card_visible() {
        const int s = card_state.load(std::memory_order_relaxed);
        return s < 0 ? aidyn::settings::graphics().controls_card : s != 0;
    }

    void open_first_pad() {
        // A test run (AIDYN_PAD_FILE, tools/dev/pad.sh) opens no controller: the game reads controllers in the
        // background too, and presses meant for something else on the desktop would steer the test.
        static const bool test_run = std::getenv("AIDYN_PAD_FILE") != nullptr;
        if (pad != nullptr || test_run) {
            return;
        }
        for (int i = 0; i < SDL_NumJoysticks(); i++) {
            if (SDL_IsGameController(i)) {
                pad = SDL_GameControllerOpen(i);
                if (pad != nullptr) {
                    fprintf(stdout, "Controller: %s\n", SDL_GameControllerName(pad));
                    return;
                }
            }
        }
    }

    float axis(SDL_GameControllerAxis a) {
        float v = SDL_GameControllerGetAxis(pad, a) / 32767.0f;
        return std::clamp(v, -1.0f, 1.0f);
    }

    float apply_deadzone(float v) {
        float mag = std::fabs(v);
        if (mag < stick_deadzone) {
            return 0.0f;
        }
        return std::copysign((mag - stick_deadzone) / (1.0f - stick_deadzone), v);
    }
}

void aidyn::input::handle_event(const SDL_Event& event) {
    std::lock_guard lock{ pad_mutex };
    switch (event.type) {
    case SDL_CONTROLLERDEVICEADDED:
        open_first_pad();
        break;
    case SDL_CONTROLLERDEVICEREMOVED:
        if (pad != nullptr && event.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) {
            SDL_GameControllerClose(pad);
            pad = nullptr;
            open_first_pad();
        }
        break;
    default:
        break;
    }
}

void aidyn::input::poll() {
    // Pads are opened from SDL's device-added events (and once at startup); scanning SDL's device list on every
    // controller poll from the game's input thread was needless, and raced SDL's own thread.
    std::lock_guard lock{ pad_mutex };
    if (!scanned) {
        scanned = true;
        open_first_pad();
    }
}

bool aidyn::input::get(int controller_num, uint16_t* buttons, float* x, float* y) {
    if (controller_num != 0) {
        return false;
    }

    uint16_t b = 0;
    float sx = 0.0f, sy = 0.0f, rx = 0.0f, ry = 0.0f;
    bool click = false;
    bool l3 = false;

    {
        std::lock_guard lock{ pad_mutex };
        if (pad != nullptr) {
            auto down = [&](SDL_GameControllerButton btn) { return SDL_GameControllerGetButton(pad, btn) != 0; };
            // Xbox-style pad -> N64. The right stick is the camera (src/game/patches.cpp), so the C buttons are on
            // face buttons and RT: X = C-down (Details; in combat, spells), Y = C-up (in combat, items and potions),
            // View = C-left and RT = C-right (previous / next party member in the pause menu).
            if (down(SDL_CONTROLLER_BUTTON_A)) b |= A;
            if (down(SDL_CONTROLLER_BUTTON_B)) b |= B;
            if (down(SDL_CONTROLLER_BUTTON_X)) b |= CD;
            if (down(SDL_CONTROLLER_BUTTON_Y)) b |= CU;
            if (down(SDL_CONTROLLER_BUTTON_BACK)) b |= CL;
            if (axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 0.3f) b |= CR;
            if (down(SDL_CONTROLLER_BUTTON_START)) b |= START;
            if (down(SDL_CONTROLLER_BUTTON_DPAD_UP)) b |= DU;
            if (down(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) b |= DD;
            if (down(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) b |= DL;
            if (down(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) b |= DR;
            if (down(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) b |= L;
            if (down(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) b |= R;
            if (axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 0.3f) b |= Z;
            click = down(SDL_CONTROLLER_BUTTON_RIGHTSTICK);  // recentre the camera
            l3 = down(SDL_CONTROLLER_BUTTON_LEFTSTICK);       // fast-forward on / off
            float cx = axis(SDL_CONTROLLER_AXIS_RIGHTX), cy = axis(SDL_CONTROLLER_AXIS_RIGHTY);
            sx = apply_deadzone(axis(SDL_CONTROLLER_AXIS_LEFTX));
            sy = -apply_deadzone(axis(SDL_CONTROLLER_AXIS_LEFTY));
            rx = apply_deadzone(cx);
            ry = -apply_deadzone(cy);
        }
    }

    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    if (keys[SDL_SCANCODE_Z]) b |= A;
    if (keys[SDL_SCANCODE_X]) b |= B;
    if (keys[SDL_SCANCODE_C]) b |= Z;
    if (keys[SDL_SCANCODE_RETURN]) b |= START;
    if (keys[SDL_SCANCODE_Q]) b |= L;
    if (keys[SDL_SCANCODE_E]) b |= R;
    if (keys[SDL_SCANCODE_I]) b |= CU;
    if (keys[SDL_SCANCODE_K]) b |= CD;
    if (keys[SDL_SCANCODE_J]) b |= CL;
    if (keys[SDL_SCANCODE_L]) b |= CR;
    if (keys[SDL_SCANCODE_KP_8]) b |= DU;
    if (keys[SDL_SCANCODE_KP_5]) b |= DD;
    if (keys[SDL_SCANCODE_KP_4]) b |= DL;
    if (keys[SDL_SCANCODE_KP_6]) b |= DR;
    if (keys[SDL_SCANCODE_RIGHT]) sx = 1.0f;
    if (keys[SDL_SCANCODE_LEFT]) sx = -1.0f;
    if (keys[SDL_SCANCODE_UP]) sy = 1.0f;
    if (keys[SDL_SCANCODE_DOWN]) sy = -1.0f;

    // Test aid for automated play-testing: AIDYN_PAD_FILE names a text file holding "<hex buttons> <x> <y> [<rx> <ry>]"
    // (N64 button bits as above, plus 0x10000 for R3, 0x20000 for L3 and 0x40000 for F2; sticks -1..1, the right stick optional). Read on every poll and
    // ORed in; absent or unreadable means nothing.
    static const char* pad_file = std::getenv("AIDYN_PAD_FILE");
    if (pad_file != nullptr) {
        std::ifstream f{ pad_file };
        unsigned fb = 0;
        float fx = 0.0f, fy = 0.0f, frx = 0.0f, fry = 0.0f;
        if (f >> std::hex >> fb >> std::dec >> fx >> fy) {
            b |= static_cast<uint16_t>(fb);
            click = click || (fb & 0x10000) != 0;
            l3 = l3 || (fb & 0x20000) != 0;
            static bool f2_was = false;  // 0x40000: F2 (redrawn art off / on)
            if ((fb & 0x40000) != 0 && !f2_was) {
                aidyn::toggle_redrawn_art();
            }
            f2_was = (fb & 0x40000) != 0;
            if (fx != 0.0f || fy != 0.0f) {
                sx = fx;
                sy = fy;
            }
            if ((f >> frx >> fry) && (frx != 0.0f || fry != 0.0f)) {
                rx = frx;
                ry = fry;
            }
        }
    }
    right_x.store(rx, std::memory_order_relaxed);
    right_y.store(ry, std::memory_order_relaxed);
    right_click.store(click, std::memory_order_relaxed);
    if (l3 && !l3_was_down) {
        ff_toggled.store(!ff_toggled.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    l3_was_down = l3;
    ff_held.store(keys[SDL_SCANCODE_TAB] != 0, std::memory_order_relaxed);
    c_down.store((b & CD) != 0, std::memory_order_relaxed);
    c_up.store((b & CU) != 0, std::memory_order_relaxed);
    start.store((b & START) != 0, std::memory_order_relaxed);
    // In the field and in combat LB shows or hides the controls card (src/game/patches.cpp, "Button prompts"), and in
    // the field View quicksaves ("Quick and auto saves"); there neither reaches the game (LB only turns the pause
    // menu's pages; in combat it was a second End turn).
    static bool view_was_down = false, lb_was_down = false;
    const bool view = (b & CL) != 0, lb = (b & L) != 0;
    const int mode = card_mode.load(std::memory_order_relaxed);
    if (mode != 0) {
        if (lb && !lb_was_down) {
            const bool shown = !card_visible();
            card_state.store(shown ? 1 : 0, std::memory_order_relaxed);
            aidyn::settings::save_controls_card(shown);
        }
        if (mode == 1 && view && !view_was_down) {
            quicksave_pressed.store(true, std::memory_order_relaxed);
        }
        b &= ~(CL | L);
    }
    view_was_down = view;
    lb_was_down = lb;

    captured_buttons.store(b, std::memory_order_relaxed);
    captured_x.store(sx, std::memory_order_relaxed);
    captured_y.store(sy, std::memory_order_relaxed);
    if (capture.load(std::memory_order_relaxed)) {
        b = 0;
        sx = sy = 0.0f;
    }
    *buttons = b;
    *x = sx;
    *y = sy;
    return true;
}

void aidyn::input::set_card_mode(int mode) {
    card_mode.store(mode, std::memory_order_relaxed);
}

bool aidyn::input::quicksave_asked() {
    return quicksave_pressed.exchange(false, std::memory_order_relaxed);
}

bool aidyn::input::start_held() {
    return start.load(std::memory_order_relaxed);
}

bool aidyn::input::controls_card() {
    return card_visible();
}

void aidyn::input::right_stick(float* x, float* y) {
    *x = right_x.load(std::memory_order_relaxed);
    *y = right_y.load(std::memory_order_relaxed);
}

void aidyn::input::set_controls_card(bool shown) {
    card_state.store(shown ? 1 : 0, std::memory_order_relaxed);
    aidyn::settings::save_controls_card(shown);
}

void aidyn::input::set_capture(bool on) {
    capture.store(on, std::memory_order_relaxed);
}

void aidyn::input::captured(uint32_t* buttons, float* x, float* y) {
    *buttons = captured_buttons.load(std::memory_order_relaxed);
    *x = captured_x.load(std::memory_order_relaxed);
    *y = captured_y.load(std::memory_order_relaxed);
}

bool aidyn::input::c_down_held() {
    return c_down.load(std::memory_order_relaxed);
}

bool aidyn::input::c_up_held() {
    return c_up.load(std::memory_order_relaxed);
}

bool aidyn::input::fast_forward() {
    return ff_toggled.load(std::memory_order_relaxed) || ff_held.load(std::memory_order_relaxed);
}

bool aidyn::input::right_stick_click() {
    return right_click.load(std::memory_order_relaxed);
}

void aidyn::input::set_rumble(int controller_num, bool rumble) {
    std::lock_guard lock{ pad_mutex };
    if (controller_num == 0 && pad != nullptr) {
        SDL_GameControllerRumble(pad, rumble ? 0xFFFF : 0, rumble ? 0xFFFF : 0, rumble ? 0xFFFFFFFF : 0);
    }
}
