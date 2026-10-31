// Keyboard and mouse controls: see controls.h. The bindings live in settings.json:
//   "controls": { "mouse_look": true, "mouse_sensitivity": 1.0, "mouse_invert_y": false,
//                 "bindings": { "a": ["Z", "Mouse Left"], "b": ["X", "Mouse Right"], "move_up": ["W", "Up"], ... } }
// A key by SDL's name ("Z", "Left Shift", "Keypad 8", "Return"), a mouse button ("Mouse Left", "Mouse Middle", "Mouse
// Right", "Mouse Back", "Mouse Forward") or a wheel step ("Wheel Up", "Wheel Down"); at most two to an action, [] for
// none. An action missing from the file keeps its default; a name nobody knows is left out (and logged).
// AIDYN_INPUT_FILE=<file> (testing, tools/dev/pad.sh): a line of inputs held "W, Mouse Left", and "look=dx,dy" (mouse
// counts a second); re-read on every poll.
#include "controls.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <vector>

#include "SDL2/SDL.h"
#include "json/json.hpp"

#include "settings.h"

namespace aidyn::controls {
namespace {
    constexpr Input key(SDL_Scancode c) { return Input{ kKey, (uint16_t)c }; }
    constexpr Input mouse(int b) { return Input{ kMouse, (uint16_t)b }; }
    constexpr Input none() { return Input{}; }

    struct Entry {
        ActionInfo info;
        Input defaults[kSlots];
    };

    // Keys the way the program has always had them, plus W A S D, the mouse and the camera's keys.
    const Entry kEntries[kActionCount] = {
        { { "move_up", "Move up" }, { key(SDL_SCANCODE_W), key(SDL_SCANCODE_UP) } },
        { { "move_down", "Move down" }, { key(SDL_SCANCODE_S), key(SDL_SCANCODE_DOWN) } },
        { { "move_left", "Move left" }, { key(SDL_SCANCODE_A), key(SDL_SCANCODE_LEFT) } },
        { { "move_right", "Move right" }, { key(SDL_SCANCODE_D), key(SDL_SCANCODE_RIGHT) } },
        { { "dpad_up", "D-pad up" }, { key(SDL_SCANCODE_KP_8), none() } },
        { { "dpad_down", "D-pad down" }, { key(SDL_SCANCODE_KP_5), none() } },
        { { "dpad_left", "D-pad left" }, { key(SDL_SCANCODE_KP_4), none() } },
        { { "dpad_right", "D-pad right" }, { key(SDL_SCANCODE_KP_6), none() } },
        { { "a", "A  Talk, confirm" }, { key(SDL_SCANCODE_Z), mouse(SDL_BUTTON_LEFT) } },
        { { "b", "B  Back, cancel" }, { key(SDL_SCANCODE_X), mouse(SDL_BUTTON_RIGHT) } },
        { { "x", "X  Details, spells" }, { key(SDL_SCANCODE_K), none() } },
        { { "y", "Y  Items" }, { key(SDL_SCANCODE_I), none() } },
        { { "view", "View  Controls card" }, { key(SDL_SCANCODE_J), none() } },
        { { "lt", "LT  Sneak, end turn" }, { key(SDL_SCANCODE_C), mouse(SDL_BUTTON_X1) } },
        { { "rt", "RT  Next member" }, { key(SDL_SCANCODE_L), mouse(SDL_BUTTON_X2) } },
        { { "lb", "LB  Quicksave, page back" }, { key(SDL_SCANCODE_Q), none() } },
        { { "rb", "RB  Page forward" }, { key(SDL_SCANCODE_E), none() } },
        { { "menu", "Menu  Pause" }, { key(SDL_SCANCODE_RETURN), none() } },
        { { "camera_up", "Camera up" }, { key(SDL_SCANCODE_SEMICOLON), none() } },
        { { "camera_down", "Camera down" }, { key(SDL_SCANCODE_APOSTROPHE), none() } },
        { { "camera_left", "Camera left" }, { key(SDL_SCANCODE_LEFTBRACKET), none() } },
        { { "camera_right", "Camera right" }, { key(SDL_SCANCODE_RIGHTBRACKET), none() } },
        { { "camera_reset", "Camera behind" }, { mouse(SDL_BUTTON_MIDDLE), none() } },
        { { "fast_forward", "Fast-forward" }, { key(SDL_SCANCODE_TAB), none() } },
    };

    std::mutex g_lock;
    Input g_bind[kActionCount][kSlots];
    bool g_loaded = false;
    bool g_mouse_look = true;
    float g_sensitivity = 1.0f;
    bool g_invert_y = false;

    // What the window has told us (main thread writes, the game's input thread reads).
    std::mutex g_event_lock;
    std::deque<Input> g_pressed;
    bool g_listening = false;
    std::chrono::steady_clock::time_point g_wheel_until[2];
    int g_motion_x = 0, g_motion_y = 0;  // mouse counts since the camera last asked
    std::atomic<int64_t> g_look_asked_ms{ 0 };
    std::atomic_bool g_want_capture{ false };
    // The pointer in the window (SDL_MOUSEMOTION while not captured) and the window's size then; a count of moves.
    int g_pointer_x = 0, g_pointer_y = 0, g_window_w = 0, g_window_h = 0;
    uint32_t g_pointer_moves = 0;

    int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    // Degrees of turn for one mouse count at sensitivity 1.
    constexpr float kDegreesPerCount = 0.10f;

    // ---- names ----
    const char* kMouseNames[] = { "", "Mouse Left", "Mouse Middle", "Mouse Right", "Mouse Back", "Mouse Forward" };

    bool equals_nocase(const std::string& a, const char* b) {
        return SDL_strcasecmp(a.c_str(), b) == 0;
    }

    Input parse_input(const std::string& text) {
        if (text.empty()) return none();
        for (int b = 1; b <= 5; b++) {
            if (equals_nocase(text, kMouseNames[b])) return mouse(b);
        }
        if (equals_nocase(text, "Wheel Up")) return Input{ kWheel, 0 };
        if (equals_nocase(text, "Wheel Down")) return Input{ kWheel, 1 };
        if (equals_nocase(text, "Enter")) return key(SDL_SCANCODE_RETURN);
        if (equals_nocase(text, "Esc")) return key(SDL_SCANCODE_ESCAPE);
        const SDL_Scancode sc = SDL_GetScancodeFromName(text.c_str());
        if (sc != SDL_SCANCODE_UNKNOWN) return key(sc);
        return none();
    }

    std::string json_name(Input in) {
        switch (in.kind) {
        case kKey: return SDL_GetScancodeName((SDL_Scancode)in.code);
        case kMouse: return in.code >= 1 && in.code <= 5 ? kMouseNames[in.code] : "";
        case kWheel: return in.code == 0 ? "Wheel Up" : "Wheel Down";
        default: return "";
        }
    }

    // ---- loading and saving ----
    void set_defaults() {
        for (int a = 0; a < kActionCount; a++) {
            for (int s = 0; s < kSlots; s++) g_bind[a][s] = kEntries[a].defaults[s];
        }
        g_mouse_look = true;
        g_sensitivity = 1.0f;
        g_invert_y = false;
    }

    void ensure_loaded() {
        if (g_loaded) return;
        g_loaded = true;
        set_defaults();
        const std::filesystem::path path = aidyn::settings::folder() / "settings.json";
        std::ifstream in(path);
        if (!in) return;
        try {
            const nlohmann::json j = nlohmann::json::parse(in, nullptr, true, true);
            const auto section = j.find("controls");
            if (section == j.end() || !section->is_object()) return;
            g_mouse_look = section->value("mouse_look", g_mouse_look);
            g_sensitivity = std::clamp(section->value("mouse_sensitivity", g_sensitivity), 0.1f, 5.0f);
            g_invert_y = section->value("mouse_invert_y", g_invert_y);
            const auto bindings = section->find("bindings");
            if (bindings == section->end() || !bindings->is_object()) return;
            for (int a = 0; a < kActionCount; a++) {
                const auto entry = bindings->find(kEntries[a].info.id);
                if (entry == bindings->end() || !entry->is_array()) continue;
                int s = 0;
                for (int s2 = 0; s2 < kSlots; s2++) g_bind[a][s2] = none();
                for (const auto& v : *entry) {
                    if (!v.is_string() || s >= kSlots) continue;
                    const Input parsed = parse_input(v.get<std::string>());
                    if (parsed.empty()) {
                        std::fprintf(stdout, "Controls: settings.json controls.bindings.%s: \"%s\" is not a key or button the game knows\n",
                                     kEntries[a].info.id, v.get<std::string>().c_str());
                        continue;
                    }
                    g_bind[a][s++] = parsed;
                }
            }
        }
        catch (const nlohmann::json::exception& e) {
            std::fprintf(stdout, "Controls: settings.json's controls section can't be read (%s): the defaults are used\n", e.what());
            set_defaults();
        }
    }

    void save() {  // g_lock held
        nlohmann::ordered_json section;
        section["mouse_look"] = g_mouse_look;
        section["mouse_sensitivity"] = std::round(g_sensitivity * 100.0f) / 100.0f;
        section["mouse_invert_y"] = g_invert_y;
        nlohmann::ordered_json bindings;
        for (int a = 0; a < kActionCount; a++) {
            nlohmann::ordered_json list = nlohmann::ordered_json::array();
            for (int s = 0; s < kSlots; s++) {
                if (!g_bind[a][s].empty()) list.push_back(json_name(g_bind[a][s]));
            }
            bindings[kEntries[a].info.id] = list;
        }
        section["bindings"] = bindings;
        aidyn::settings::save_section("controls", section.dump());
    }

    // ---- the test file ----
    struct TestInput {
        std::vector<Input> held;
        float look_x = 0.0f, look_y = 0.0f;
        float point_x = -1.0f, point_y = -1.0f;  // point=x,y: the pointer in the menus' 320 x 240 units
    };

    TestInput read_test_file() {
        static const char* path = std::getenv("AIDYN_INPUT_FILE");
        TestInput t;
        if (path == nullptr) return t;
        std::ifstream f(path);
        std::string line;
        if (!std::getline(f, line)) return t;
        std::stringstream ss(line);
        std::string token;
        while (std::getline(ss, token, ',')) {
            const size_t a = token.find_first_not_of(' '), b = token.find_last_not_of(' ');
            if (a == std::string::npos) continue;
            token = token.substr(a, b - a + 1);
            if (token.rfind("point=", 0) == 0) {
                std::string y;
                std::getline(ss, y, ',');
                t.point_x = (float)std::atof(token.c_str() + 6);
                t.point_y = (float)std::atof(y.c_str());
                continue;
            }
            if (token.rfind("look=", 0) == 0) {
                std::string y;
                std::getline(ss, y, ',');
                t.look_x = (float)std::atof(token.c_str() + 5);
                t.look_y = (float)std::atof(y.c_str());
                continue;
            }
            const Input in = parse_input(token);
            if (!in.empty()) t.held.push_back(in);
        }
        return t;
    }

    bool held(Input in, const TestInput& test) {
        if (std::find(test.held.begin(), test.held.end(), in) != test.held.end()) return true;
        switch (in.kind) {
        case kKey: {
            const Uint8* keys = SDL_GetKeyboardState(nullptr);
            return keys != nullptr && keys[in.code] != 0;
        }
        case kMouse:
            return (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(in.code)) != 0;
        case kWheel: {
            std::lock_guard g(g_event_lock);
            return std::chrono::steady_clock::now() < g_wheel_until[in.code & 1];
        }
        default:
            return false;
        }
    }
}

const ActionInfo& info(Action a) {
    return kEntries[a].info;
}

Input binding(Action a, int slot) {
    std::lock_guard g(g_lock);
    ensure_loaded();
    return g_bind[a][slot];
}

bool is_escape(Input in) {
    return in.kind == kKey && in.code == SDL_SCANCODE_ESCAPE;
}

bool reserved(Input in) {
    if (in.kind != kKey) return false;
    return in.code == SDL_SCANCODE_ESCAPE || in.code == SDL_SCANCODE_F1 || in.code == SDL_SCANCODE_F2 || in.code == SDL_SCANCODE_F11;
}

int set_binding(Action a, int slot, Input in) {
    std::lock_guard g(g_lock);
    ensure_loaded();
    int moved_from = -1;
    for (int o = 0; o < kActionCount; o++) {
        for (int s = 0; s < kSlots; s++) {
            if (!(o == a && s == slot) && g_bind[o][s] == in) {
                g_bind[o][s] = none();
                moved_from = o;
            }
        }
    }
    g_bind[a][slot] = in;
    save();
    return moved_from;
}

void clear_binding(Action a, int slot) {
    std::lock_guard g(g_lock);
    ensure_loaded();
    g_bind[a][slot] = none();
    save();
}

void reset_defaults() {
    std::lock_guard g(g_lock);
    ensure_loaded();
    set_defaults();
    save();
}

std::string input_name(Input in) {
    switch (in.kind) {
    case kKey: {
        if (in.code == SDL_SCANCODE_RETURN) return "Enter";
        if (in.code == SDL_SCANCODE_ESCAPE) return "Esc";
        // The game's letters have no punctuation to speak of: those keys by name.
        static const struct { SDL_Scancode code; const char* name; } kNamed[] = {
            { SDL_SCANCODE_LEFTBRACKET, "Left Bracket" }, { SDL_SCANCODE_RIGHTBRACKET, "Right Bracket" },
            { SDL_SCANCODE_SEMICOLON, "Semicolon" }, { SDL_SCANCODE_APOSTROPHE, "Apostrophe" },
            { SDL_SCANCODE_COMMA, "Comma" }, { SDL_SCANCODE_PERIOD, "Period" }, { SDL_SCANCODE_SLASH, "Slash" },
            { SDL_SCANCODE_BACKSLASH, "Backslash" }, { SDL_SCANCODE_MINUS, "Minus" }, { SDL_SCANCODE_EQUALS, "Equals" },
            { SDL_SCANCODE_GRAVE, "Grave" },
        };
        for (const auto& k : kNamed) {
            if (in.code == k.code) return k.name;
        }
        std::string n = SDL_GetScancodeName((SDL_Scancode)in.code);
        return n.empty() ? "Key " + std::to_string(in.code) : n;
    }
    case kMouse: return in.code >= 1 && in.code <= 5 ? kMouseNames[in.code] : "Mouse";
    case kWheel: return in.code == 0 ? "Wheel Up" : "Wheel Down";
    default: return "-";
    }
}

std::string action_keys(Action a) {
    std::string out;
    for (int s = 0; s < kSlots; s++) {
        const Input in = binding(a, s);
        if (in.empty()) continue;
        if (!out.empty()) out += " / ";
        out += input_name(in);
    }
    return out.empty() ? "-" : out;
}

bool down(Action a) {
    Input in[kSlots];
    {
        std::lock_guard g(g_lock);
        ensure_loaded();
        for (int s = 0; s < kSlots; s++) in[s] = g_bind[a][s];
    }
    const TestInput test = read_test_file();
    for (int s = 0; s < kSlots; s++) {
        if (!in[s].empty() && held(in[s], test)) return true;
    }
    return false;
}

bool mouse_look() {
    std::lock_guard g(g_lock);
    ensure_loaded();
    return g_mouse_look;
}

void set_mouse_look(bool on) {
    std::lock_guard g(g_lock);
    ensure_loaded();
    g_mouse_look = on;
    save();
}

float mouse_sensitivity() {
    std::lock_guard g(g_lock);
    ensure_loaded();
    return g_sensitivity;
}

void set_mouse_sensitivity(float s) {
    std::lock_guard g(g_lock);
    ensure_loaded();
    g_sensitivity = std::clamp(s, 0.1f, 5.0f);
    save();
}

bool mouse_invert_y() {
    std::lock_guard g(g_lock);
    ensure_loaded();
    return g_invert_y;
}

void set_mouse_invert_y(bool on) {
    std::lock_guard g(g_lock);
    ensure_loaded();
    g_invert_y = on;
    save();
}

void look(float* turn_degrees, float* tilt_degrees) {
    g_look_asked_ms = now_ms();
    float sens;
    bool invert, enabled;
    {
        std::lock_guard g(g_lock);
        ensure_loaded();
        sens = g_sensitivity;
        invert = g_invert_y;
        enabled = g_mouse_look;
    }
    int dx, dy;
    {
        std::lock_guard g(g_event_lock);
        dx = g_motion_x;
        dy = g_motion_y;
        g_motion_x = g_motion_y = 0;
    }
    float fx = (float)dx, fy = (float)dy;
    const TestInput test = read_test_file();
    fx += test.look_x / 60.0f;  // a second's counts, one game tick of it
    fy += test.look_y / 60.0f;
    *turn_degrees = enabled ? fx * kDegreesPerCount * sens : 0.0f;
    float tilt = enabled ? -fy * kDegreesPerCount * sens : 0.0f;  // the mouse moving up (y falling) looks up
    *tilt_degrees = invert ? -tilt : tilt;
}

void want_capture(bool on) {
    g_want_capture = on;
}

void apply_mouse_mode() {
    // A test window never takes the mouse (AIDYN_TEST_MOUSE=1: it may, to try it).
    static const bool test_mouse = std::getenv("AIDYN_TEST_MOUSE") != nullptr;
    static const bool test_run = std::getenv("AIDYN_PAD_FILE") != nullptr && !test_mouse;
    // Only while the window has the keyboard focus: SDL lets go of the mouse when it loses it, and asking again
    // from the background would take it back.
    const bool want = g_want_capture.load() && mouse_look() && !test_run && (SDL_GetKeyboardFocus() != nullptr || test_mouse);
    if (want != (SDL_GetRelativeMouseMode() == SDL_TRUE)) {
        SDL_SetRelativeMouseMode(want ? SDL_TRUE : SDL_FALSE);
        static const bool log = std::getenv("AIDYN_CONTROLS_LOG") != nullptr;
        if (log) {
            std::fprintf(stdout, "Controls: mouse %s\n", want ? "captured for the camera" : "released");
            std::fflush(stdout);
        }
    }
}

void handle_event(const SDL_Event& event) {
    switch (event.type) {
    case SDL_MOUSEMOTION: {
        // The pointer, for the menus (pointer()): where it is in the window, while the mouse isn't the camera's.
        if (SDL_GetRelativeMouseMode() != SDL_TRUE) {
            if (SDL_Window* w = SDL_GetWindowFromID(event.motion.windowID)) {
                int ww = 0, wh = 0;
                SDL_GetWindowSize(w, &ww, &wh);
                std::lock_guard g(g_event_lock);
                g_pointer_x = event.motion.x, g_pointer_y = event.motion.y, g_window_w = ww, g_window_h = wh;
                g_pointer_moves++;
            }
        }
        // Only while the camera is asking (the field): menus and fights would pile it up for later.
        if (now_ms() - g_look_asked_ms.load() < 200) {
            std::lock_guard g(g_event_lock);
            g_motion_x += event.motion.xrel;
            g_motion_y += event.motion.yrel;
        }
        break;
    }
    case SDL_MOUSEWHEEL: {
        if (event.wheel.y == 0) break;
        const int dir = (event.wheel.y > 0) == (event.wheel.direction != SDL_MOUSEWHEEL_FLIPPED) ? 0 : 1;
        std::lock_guard g(g_event_lock);
        g_wheel_until[dir] = std::chrono::steady_clock::now() + std::chrono::milliseconds(120);
        if (g_listening) g_pressed.push_back(Input{ kWheel, (uint16_t)dir });
        break;
    }
    case SDL_MOUSEBUTTONDOWN: {
        std::lock_guard g(g_event_lock);
        if (g_listening) g_pressed.push_back(mouse(event.button.button));
        break;
    }
    case SDL_KEYDOWN: {
        if (event.key.repeat) break;
        std::lock_guard g(g_event_lock);
        if (g_listening) g_pressed.push_back(key(event.key.keysym.scancode));
        break;
    }
    default:
        break;
    }
}

bool pointer(float* x, float* y, uint32_t* moves) {
    const TestInput test = read_test_file();
    if (test.point_x >= 0.0f) {  // AIDYN_INPUT_FILE's point=x,y (tests): moved whenever it changes
        static float last_x = -1.0f, last_y = -1.0f;
        static uint32_t n = 0;
        if (test.point_x != last_x || test.point_y != last_y) n++;
        last_x = test.point_x, last_y = test.point_y;
        *x = test.point_x, *y = test.point_y, *moves = n | 0x80000000u;
        return true;
    }
    std::lock_guard g(g_event_lock);
    if (g_window_h <= 0 || g_pointer_moves == 0) return false;
    // The menus are the picture's middle 4:3 (or the whole of a narrower window), 240 units tall.
    const float unit = std::min((float)g_window_h / 240.0f, (float)g_window_w / 320.0f);
    *x = (g_pointer_x - (g_window_w - 320.0f * unit) / 2.0f) / unit;
    *y = (g_pointer_y - (g_window_h - 240.0f * unit) / 2.0f) / unit;
    *moves = g_pointer_moves;
    return true;
}

void listen(bool on) {
    std::lock_guard g(g_event_lock);
    g_listening = on;
    g_pressed.clear();
}

bool take_pressed(Input* in) {
    // The test file's newly held inputs count as presses (AIDYN_INPUT_FILE).
    static std::vector<Input> before;
    const TestInput test = read_test_file();
    std::lock_guard g(g_event_lock);
    if (g_listening) {
        for (const Input& t : test.held) {
            if (std::find(before.begin(), before.end(), t) == before.end()) g_pressed.push_back(t);
        }
    }
    before = test.held;
    if (g_pressed.empty()) return false;
    *in = g_pressed.front();
    g_pressed.pop_front();
    return true;
}

}  // namespace aidyn::controls
