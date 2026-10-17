// Game functions replaced by hand-written C (listed in aidyn.us.toml's `ignored` and in README.md).
#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"

#include "recomp.h"
#include "librecomp/helpers.hpp"

#include "settings.h"

namespace {
    constexpr int32_t gGfxManager = (int32_t)0x800D7ED8;
    constexpr int kHres1 = 0x192;   // gGfxManager.Hres[1]: width of the frame being built
    constexpr int kVres1 = 0x196;   // gGfxManager.Vres[1]
    constexpr int kUnk19C = 0x19C;  // gGfxManager.unk0x19c, cleared at the end of every frame

    void put(uint8_t* rdram, gpr& g, uint32_t w0, uint32_t w1) {
        MEM_W(0, g) = w0;
        MEM_W(4, g) = w1;
        g += 8;
    }

    // gDPFillRectangle(ulx, uly, lrx, lry)
    void fill_rect(uint8_t* rdram, gpr& g, uint32_t ulx, uint32_t uly, uint32_t lrx, uint32_t lry) {
        put(rdram, g, 0xF6000000 | ((lrx & 0x3FF) << 14) | ((lry & 0x3FF) << 2), ((ulx & 0x3FF) << 14) | ((uly & 0x3FF) << 2));
    }
}

// Graphics::EndList(Gfx* g) -> Gfx*: ends each frame's display list. The original first paints black bars over
// the TV overscan area (16 px at the sides, 12 at the top and bottom; 20 at the sides in 640-wide mode), which a
// CRT hid but a modern display shows as a frame, and which widescreen moves into the middle of the picture.
// They are drawn only when settings.json has "overscan_borders": true; everything else is as the original.
gpr aidyn_ff_marker(uint8_t* rdram, gpr g);  // "Fast-forward", below
gpr aidyn_prompts(uint8_t* rdram, gpr g);    // "Button prompts", below

extern "C" void Graphics__EndList(uint8_t* rdram, recomp_context* ctx) {
    gpr g = ctx->r4;
    g = aidyn_ff_marker(rdram, g);
    g = aidyn_prompts(rdram, g);
    if (aidyn::settings::graphics().overscan_borders) {
        const uint32_t h = MEM_HU(kHres1, (gpr)gGfxManager);
        const uint32_t v = MEM_HU(kVres1, (gpr)gGfxManager);
        const uint32_t side = (h == 320) ? 16 : 20;
        put(rdram, g, 0xE7000000, 0);          // gDPPipeSync
        put(rdram, g, 0xE3000A01, 0x00300000); // gDPSetCycleType(G_CYC_FILL)
        put(rdram, g, 0xE200001C, 0);          // gDPSetRenderMode(0, 0)
        put(rdram, g, 0xF7000000, 0);          // gDPSetFillColor(0)
        fill_rect(rdram, g, 0, 0, side - 1, v - 1);
        fill_rect(rdram, g, 0, 0, h - 1, 11);
        fill_rect(rdram, g, h - side, 0, h - 1, v - 1);
        fill_rect(rdram, g, 0, v - 12, h - 1, v - 1);
    }
    put(rdram, g, 0xE9000000, 0);              // gDPFullSync
    put(rdram, g, 0xDF000000, 0);              // gSPEndDisplayList
    MEM_B(kUnk19C, (gpr)gGfxManager) = 0;
    // osWritebackDCacheAll: nothing to write back on the runtime.
    ctx->r2 = g;
}

// AIDYN_UI_LOG=<folder>: every 2D image the game draws goes through N64BorgImageDraw(g, Borg8Header* img, ...)
// (menus, HUD, fonts, portraits). aidyn.us.toml hooks its first instruction to call this, which saves each image
// once, whole, as <folder>/<pixel address>.bin (+ .pal for palettes) with a line in <folder>/images.jsonl.
// tools/hdui uses these to know which RT64 textures are UI, and to upscale each image in one piece. Off unless set.
#include <atomic>
#include <cstdio>
#include <unordered_map>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>

// AIDYN_PAGE_LOG=1: every full-screen picture's draw (pause page backgrounds) with its position and clip, printed
// when they change, and its pixels' address for src/main/rt64_context.cpp, which reports when they lie in memory
// RT64 tracks as a framebuffer. For finding why a page sometimes shows its original picture. Off unless set.
std::atomic_uint32_t aidyn_page_log_image_start{0}, aidyn_page_log_image_end{0};

static void log_page_draw(uint8_t* rdram, recomp_context* ctx) {
    const gpr img = ctx->r5;
    if (img == 0 || MEM_HU(0x0A, img) != 320 || MEM_HU(0x0C, img) != 240) {
        return;
    }
    static const uint32_t bpp[] = { 0, 32, 16, 16, 8, 8, 8, 4, 4, 4 };
    const uint32_t format = MEM_HU(0x08, img);
    const uint32_t pixels = (uint32_t)MEM_W(0x14, img) & 0x3FFFFFF;
    if (format < 1 || format > 9) {
        return;
    }
    aidyn_page_log_image_start = pixels;
    aidyn_page_log_image_end = pixels + 320 * 240 * bpp[format] / 8;
    // N64BorgImageDraw(g, img, float x, float y, u16 xOff, u16 yOff, u16 h, u16 v, float xScale, float yScale, ...):
    // x and y arrive in a2 and a3, the rest on the stack from sp+0x10.
    const gpr sp = ctx->r29;
    float x, y, xs, ys;
    uint32_t w;
    w = (uint32_t)ctx->r6; std::memcpy(&x, &w, 4);
    w = (uint32_t)ctx->r7; std::memcpy(&y, &w, 4);
    w = (uint32_t)MEM_W(0x20, sp); std::memcpy(&xs, &w, 4);
    w = (uint32_t)MEM_W(0x24, sp); std::memcpy(&ys, &w, 4);
    char line[160];
    snprintf(line, sizeof(line), "page draw: fmt %u pixels %06X at %.2f,%.2f off %u,%u to %u,%u scale %.3f,%.3f",
             format, pixels, x, y, MEM_W(0x10, sp) & 0xFFFF, MEM_W(0x14, sp) & 0xFFFF, MEM_W(0x18, sp) & 0xFFFF,
             MEM_W(0x1C, sp) & 0xFFFF, xs, ys);
    static std::mutex mutex;
    static std::set<std::string> printed;
    std::lock_guard lock(mutex);
    if (printed.insert(line).second) {
        fprintf(stdout, "%s\n", line);
        fflush(stdout);
    }
}

void normalize_big_draw(uint8_t* rdram, recomp_context* ctx);  // below, "Big pictures"

void note_page_arrow(uint8_t* rdram, recomp_context* ctx);  // "Button prompts", below

extern "C" void aidyn_ui_image_seen(uint8_t* rdram, recomp_context* ctx) {
    note_page_arrow(rdram, ctx);
    static const bool page_log = std::getenv("AIDYN_PAGE_LOG") != nullptr;
    if (page_log) {
        log_page_draw(rdram, ctx);
    }
    normalize_big_draw(rdram, ctx);
    const gpr img = ctx->r5;
    static const char* dir = std::getenv("AIDYN_UI_LOG");
    if (dir == nullptr || *dir == '\0' || img == 0) {
        return;
    }
    static std::mutex mutex;
    static std::set<uint64_t> seen;
    const uint32_t format = MEM_HU(0x08, img);
    const uint32_t width = MEM_HU(0x0A, img);
    const uint32_t height = MEM_HU(0x0C, img);
    const uint32_t palette = (uint32_t)MEM_W(0x10, img);
    const uint32_t pixels = (uint32_t)MEM_W(0x14, img);
    // Bits per pixel for Borg8Format 1..9: RGBA32 RGBA16 IA16 CI8 IA8 I8 CI4 IA4 I4.
    static const uint32_t bpp[] = { 0, 32, 16, 16, 8, 8, 8, 4, 4, 4 };
    if (format < 1 || format > 9 || width == 0 || height == 0 || width > 1024 || height > 1024) {
        return;
    }
    // The game reuses memory: a page's background can later load where another picture used to be. Images are told
    // apart by address and content (FNV-1a over pixels and palette), so each distinct picture is saved once.
    const uint32_t bytes = width * height * bpp[format] / 8;
    const uint32_t palette_entries = (format == 4) ? 256 : (format == 7) ? 16 : 0;
    uint64_t hash = 0xCBF29CE484222325ULL;
    auto mix = [&](uint32_t address, uint32_t count) {
        for (uint32_t i = 0; i < count; i++) {
            hash = (hash ^ MEM_BU(i, (gpr)(int32_t)address)) * 0x100000001B3ULL;
        }
    };
    mix(pixels, bytes);
    if (palette_entries != 0 && palette != 0) {
        mix(palette, palette_entries * 2);
    }
    std::lock_guard lock(mutex);
    if (!seen.insert(hash ^ pixels).second) {
        return;
    }
    std::filesystem::create_directories(dir);
    char name[48];
    snprintf(name, sizeof(name), "%08X_%016llX", pixels, (unsigned long long)hash);
    auto save = [&](const std::string& path, uint32_t address, uint32_t count) {
        FILE* f = fopen(path.c_str(), "wb");
        if (f == nullptr) {
            return;
        }
        for (uint32_t i = 0; i < count; i++) {
            fputc(MEM_BU(i, (gpr)(int32_t)address), f);
        }
        fclose(f);
    };
    save(std::string(dir) + "/" + name + ".bin", pixels, bytes);
    if (palette_entries != 0 && palette != 0) {
        save(std::string(dir) + "/" + name + ".pal", palette, palette_entries * 2);
    }
    if (FILE* log = fopen((std::string(dir) + "/images.jsonl").c_str(), "a")) {
        fprintf(log, "{\"pixels\": \"%08X\", \"file\": \"%s\", \"format\": %u, \"width\": %u, \"height\": %u, \"palette\": \"%08X\", \"borg\": %d}\n",
                pixels, name, format, width, height, palette, (int)MEM_W(0x00, img));
        fclose(log);
    }
}

// Menus drawn this frame. RT64's widescreen keeps some 2D draws centred and stretches others, which pulls apart menus
// that mix both (the equipment page's slot frames slid away from their icons; Level/Gold went missing). The pause menu
// (PauseWidget::Render, every page, also the combat inventory) and the equipment doll in shops (WidgetDollMenu's
// Render, 0x80030430 from its vtable at 0x800CF3D0) call this through aidyn.us.toml hooks; rt64_context.cpp shows
// those frames in 4:3.
#include <atomic>
std::atomic_bool aidyn_menu_frame{ false };

extern "C" void aidyn_menu_drawn(void) {
    aidyn_menu_frame.store(true, std::memory_order_relaxed);
}

// The pause menu opens with a reveal: bigAssOpenCallback_1 widens gGlobals.scrollLocation, the clip every page is
// drawn through, towards 0..320 in step with the 3D frame animation, and marks the menu open when that animation ends.
// It ends a few steps early, leaving the clip at 17..303 when the menu opens on the equipment page (other pages
// similar) until the menu closes. A TV's overscan and the game's black borders hid those edges. Here they showed as a
// narrower page, and the journal slid in under that clip was cut into texture strips the HD pack doesn't have, so it
// fell back to the original parchment. aidyn.us.toml hooks the "menu open" store to finish the reveal at 0..320.
extern "C" void aidyn_menu_opened(uint8_t* rdram, gpr scroll) {
    MEM_H(0x18, scroll) = 0;    // gGlobals.scrollLocation[0]
    MEM_H(0x1A, scroll) = 320;  // gGlobals.scrollLocation[1]
}

// Right-stick camera. Walking uses Camera::Mode0 (ProcessGameCamera_mode0, 0x800A6E64). Each frame it puts the
// camera's target position (Camera_struct.posTarget, +0x18) on the line from the player (aim, +0xC) through the
// camera (pos, +0x0) at the follow distance (+0x5C) and the height angle (+0x48, degrees), with func_800A681C; then
// eases that target towards a second point, behind the player's facing turned by +0x58 degrees (func_800A68E0); and
// eases the camera towards the target. So turning the target alone was undone the next frame, and the game resets
// +0x58 whenever the player moves (func_800A67FC).
//
// Actor::Move (aidyn.us.toml hook at 0x8001532C) calls aidyn_camera_stick for every actor with s3 = the player handler
// (camera pointer at +4) and s2 = the actor. For the player, the right stick's x turns the camera itself (pos and
// posTarget) around the player, and y changes the height angle (+0x48 and the resting angle +0x4C it is reset to),
// both at the same rate, by game ticks (not the host clock, so each shown frame turns evenly). The height angle goes
// the same distance up and down from where the camera rests (walking: 15 degrees, so -15 to 45; the game itself only
// allowed 0 to 45, a third of the way up and two thirds down). graphics.camera_invert_x / camera_invert_y flip them. Once the camera has been turned, func_800A68E0 (replaced below) keeps
// the camera's own direction as its "behind" point, so the camera stays where it was put and follows at the same
// distance; pressing the right stick (R3) puts it back behind the player at its resting height, as the original. Movement is relative to
// the camera, so an angle relative to the player's facing would make the camera chase itself round in circles.
#include "input.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace {
    float read_f32(uint8_t* rdram, gpr addr) {
        uint32_t bits = (uint32_t)MEM_W(0, addr);
        float f;
        std::memcpy(&f, &bits, sizeof(f));
        return f;
    }
    void write_f32(uint8_t* rdram, gpr addr, float f) {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(f));
        MEM_W(0, addr) = (int32_t)bits;
    }

    constexpr float kTurnSpeed = 150.0f;   // degrees a second at full tilt, both axes
    constexpr float kTiltRange = 30.0f;    // degrees up and down from the resting height angle
    float rest_angle = NAN;                // the camera's resting height angle before the stick moved it
    constexpr float kDegToRad = 3.14159265f / 180.0f;
    std::atomic_bool free_camera{ false };  // turned by the right stick since the last recentre
}

extern "C" void aidyn_camera_stick(uint8_t* rdram, gpr handler, gpr actor) {
    if (!aidyn::settings::graphics().right_stick_camera || (MEM_HU(0x1E, actor) & 1) == 0) {
        return;  // not the player character
    }
    gpr cam = (gpr)(int32_t)MEM_W(4, handler);
    if (cam == 0) {
        return;
    }
    const float dt = 1.0f / 60.0f;  // called once per game tick, which is 1/60 s of game time

    static bool was_clicked = false;
    const bool clicked = aidyn::input::right_stick_click();
    if (clicked && !was_clicked) {
        free_camera = false;  // back behind the player (the original follow camera)
        if (!std::isnan(rest_angle)) {
            write_f32(rdram, cam + 0x48, rest_angle);
            write_f32(rdram, cam + 0x4C, rest_angle);
            rest_angle = NAN;
        }
    }
    was_clicked = clicked;

    float rx, ry;
    aidyn::input::right_stick(&rx, &ry);
    if (aidyn::settings::graphics().camera_invert_x) {
        rx = -rx;
    }
    if (aidyn::settings::graphics().camera_invert_y) {
        ry = -ry;
    }
    if (rx != 0.0f) {
        const float a = rx * kTurnSpeed * dt * kDegToRad;  // stick right: the view turns right
        const float c = std::cos(a), s = std::sin(a);
        const float ax = read_f32(rdram, cam + 0x0C), az = read_f32(rdram, cam + 0x14);
        for (int point : { 0x00, 0x18 }) {  // pos, posTarget
            const float dx = read_f32(rdram, cam + point) - ax, dz = read_f32(rdram, cam + point + 8) - az;
            write_f32(rdram, cam + point, ax + dx * c - dz * s);
            write_f32(rdram, cam + point + 8, az + dx * s + dz * c);
        }
        free_camera = true;
    }
    if (ry != 0.0f) {
        if (std::isnan(rest_angle)) {
            rest_angle = read_f32(rdram, cam + 0x4C);
        }
        const float lo = rest_angle - kTiltRange, hi = rest_angle + kTiltRange;
        float angle = read_f32(rdram, cam + 0x48) - ry * kTurnSpeed * dt;  // stick up: the camera dips, looking up
        angle = std::clamp(angle, lo, hi);
        write_f32(rdram, cam + 0x48, angle);
        write_f32(rdram, cam + 0x4C, angle);
        static const bool log = std::getenv("AIDYN_CAMERA_LOG") != nullptr;
        if (log) {
            fprintf(stderr, "camera: height angle %.1f (limits %.1f..%.1f), distance %.1f\n", angle, lo, hi,
                    read_f32(rdram, cam + 0x5C));
        }
    }
}

// func_800A68E0(Camera_struct* cam, vec3f* out, vec3f* facing): Mode0's "behind the player" point, the player's facing
// turned by cam+0x58 degrees at the follow distance and height angle. Same, except after the right stick has turned the
// camera: then the direction is the camera's own (Camera_struct.rotationXZ, +0x30, player minus camera), as
// func_800A681C uses, so Mode0 has nothing to swing back to.
extern "C" void func_800A68E0(uint8_t* rdram, recomp_context* ctx) {
    const gpr cam = ctx->r4, out = ctx->r5, facing = ctx->r6;
    const float dtor = read_f32(rdram, (gpr)(int32_t)0x800D5828);   // the game's degrees-to-radians constant
    const float deg180 = read_f32(rdram, (gpr)(int32_t)0x800D582C); // 180.0
    const float dist = read_f32(rdram, cam + 0x5C);
    float dx, dz;
    if (free_camera && aidyn::settings::graphics().right_stick_camera) {
        dx = -read_f32(rdram, cam + 0x30);
        dz = -read_f32(rdram, cam + 0x34);
    } else {
        const float a = read_f32(rdram, cam + 0x58) * dtor;
        const float s = std::sin(a), c = std::cos(a);
        const float fx = read_f32(rdram, facing + 0), fz = read_f32(rdram, facing + 8);
        dx = c * fx + s * fz;
        dz = c * fz - s * fx;
    }
    write_f32(rdram, out + 0, read_f32(rdram, cam + 0x0C) + dx * dist);
    write_f32(rdram, out + 4, read_f32(rdram, cam + 0x10) +
                              std::sin((deg180 - read_f32(rdram, cam + 0x48)) * dtor) * dist);
    write_f32(rdram, out + 8, read_f32(rdram, cam + 0x14) + dz * dist);
}

// Big pictures. N64BorgImageDraw(g, img, x, y, xOff, yOff, h, v, xScale, yScale, ...) draws the part [xOff, h) x
// [yOff, v) of an image. One that doesn't fit TMEM in one load (page backgrounds, title cards, the Controller Pak
// screen, map pieces, clouds) goes in strips of whole rows of that part, so every crop (a page sliding in, a reveal,
// a list redrawing behind a line of text) loads differently shaped strips, and a texture pack, which matches strips
// by their exact contents, only had the crops someone happened to capture. Here, on entry, such a draw is widened to
// the image's full width and the rows from the strip grid line at or above yOff to the image's bottom, moved so the
// requested part stays where it was; on exit (aidyn.us.toml hook before the return), each texture rectangle the
// game wrote is clipped back to the requested area, its texture coordinates moved to match, and rectangles left
// with nothing are turned into no-ops. Every draw of an image then loads the same few strips, which tools/hdui
// makes for every such image in the ROM. The two font sheets (36x414) are left alone: they are drawn a glyph at a
// time and the pack covers every glyph crop.
namespace {
    struct BigDraw {
        bool active = false;
        gpr g_in = 0, sp = 0;
        uint16_t xoff = 0, yoff = 0, h = 0, v = 0;
        int32_t cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;  // requested area, screen pixels in 10.2 fixed point
        bool title_card = false;                      // the title screen's card (188x174 RGBA16): add the branding
        float ox = 0, oy = 0, sx = 1, sy = 1;         // where the image's texel (0, 0) lands, and its scale (pixels)
    };
    BigDraw big_draw;

    // Rows between strips for a full-width load: N64BorgImageDraw's own rule (TMEM size over the padded row size, less
    // the extra row each strip loads for filtering).
    int strip_step(uint32_t format, uint32_t w) {
        auto pad = [](uint32_t n, uint32_t m) { return (n + m - 1) / m * m; };
        switch (format) {
            case 1: return (int)(0x1000 / (pad(w, 2) * 4)) - 1;                       // RGBA32
            case 2: case 3: return (int)(0x1000 / (pad(w, 4) * 2)) - 1;               // RGBA16, IA16
            case 4: return (int)(0x800 / pad(w, 8)) - 1;                              // CI8
            case 5: case 6: return (int)(0x1000 / pad(w, 8)) - 1;                     // IA8, I8
            case 7: return (int)(0x800 / (pad(w, 16) / 2)) - 1;                       // CI4
            default: return (int)(0x1000 / (pad(w, 16) / 2)) - 1;                    // IA4, I4
        }
    }
}

void normalize_big_draw(uint8_t* rdram, recomp_context* ctx) {
    big_draw.active = false;
    const gpr img = ctx->r5;
    if (img == 0) {
        return;
    }
    static const uint32_t bpp[] = { 0, 32, 16, 16, 8, 8, 8, 4, 4, 4 };
    const uint32_t format = MEM_HU(0x08, img), w = MEM_HU(0x0A, img), h_img = MEM_HU(0x0C, img);
    if (format < 1 || format > 9 || w == 0 || h_img == 0 || (w == 36 && h_img == 414)) {
        return;
    }
    const uint32_t capacity = (format == 4 || format == 7) ? 0x800 : 0x1000;
    if (w * h_img * bpp[format] / 8 <= capacity) {
        return;
    }
    const gpr sp = ctx->r29;
    const uint16_t xoff = MEM_HU(0x12, sp), yoff = MEM_HU(0x16, sp), h = MEM_HU(0x1A, sp), v = MEM_HU(0x1E, sp);
    const int step = strip_step(format, w);
    if (h <= xoff || v <= yoff || h > w || v > h_img || step < 1) {
        return;
    }
    const float hs = read_f32(rdram, (gpr)(int32_t)0x800E49A0), vs = read_f32(rdram, (gpr)(int32_t)0x800E49A4);
    const float xs = read_f32(rdram, sp + 0x20), ys = read_f32(rdram, sp + 0x24);
    float x, y;
    uint32_t bits = (uint32_t)ctx->r6;
    std::memcpy(&x, &bits, 4);
    bits = (uint32_t)ctx->r7;
    std::memcpy(&y, &bits, 4);
    // The requested area, worked out as N64BorgImageDraw does.
    const int32_t left = (int32_t)(x * hs * 4.0f);
    const int32_t top = (int32_t)(y * vs * 4.0f);
    BigDraw d;
    d.active = true;
    d.g_in = ctx->r4;
    d.sp = sp;
    d.xoff = xoff; d.yoff = yoff; d.h = h; d.v = v;
    d.cx0 = left;
    d.cx1 = left + (int32_t)((float)(int)(h - xoff) * xs * hs * 4.0f);
    d.cy0 = top;
    d.cy1 = (int32_t)(y * vs * 4.0f + (float)(int)(v - yoff) * ys * vs * 4.0f);
    d.title_card = format == 2 && w == 188 && h_img == 174;
    d.ox = (x - (float)xoff * xs) * hs;
    d.oy = (y - (float)yoff * ys) * vs;
    d.sx = xs * hs;
    d.sy = ys * vs;
    // Widen to the full width, from the strip grid line at or above yOff to the bottom.
    const uint16_t yoff2 = (uint16_t)(yoff / step * step);
    x -= (float)xoff * xs;
    y -= (float)(yoff - yoff2) * ys;
    std::memcpy(&bits, &x, 4);
    ctx->r6 = (int32_t)bits;
    std::memcpy(&bits, &y, 4);
    ctx->r7 = (int32_t)bits;
    MEM_H(0x12, sp) = 0;
    MEM_H(0x16, sp) = yoff2;
    MEM_H(0x1A, sp) = (uint16_t)w;
    MEM_H(0x1E, sp) = (uint16_t)h_img;
    big_draw = d;
}

gpr draw_title_branding(uint8_t* rdram, gpr g, float ox, float oy, float sx, float sy);  // below, "Title screen"

extern "C" void aidyn_image_draw_done(uint8_t* rdram, recomp_context* ctx) {
    if (!big_draw.active) {
        return;
    }
    const BigDraw d = big_draw;
    big_draw.active = false;
    // The caller's stack arguments go back as they were (a caller may draw again with the same ones).
    MEM_H(0x12, d.sp) = d.xoff;
    MEM_H(0x16, d.sp) = d.yoff;
    MEM_H(0x1A, d.sp) = d.h;
    MEM_H(0x1E, d.sp) = d.v;
    const gpr end = ctx->r2;
    for (gpr g = d.g_in; g + 16 < end; g += 8) {
        const uint32_t w0 = (uint32_t)MEM_W(0, g);
        if ((w0 >> 24) != 0xE4 || ((uint32_t)MEM_W(8, g) >> 24) != 0xE1 || ((uint32_t)MEM_W(16, g) >> 24) != 0xF1) {
            continue;
        }
        // gSPTextureRectangle: E4 xh yh | tile xl yl, E1 s t, F1 dsdx dtdy (screen 10.2, s/t S10.5, dsdx/dtdy S5.10).
        const uint32_t w1 = (uint32_t)MEM_W(4, g), st = (uint32_t)MEM_W(12, g), dd = (uint32_t)MEM_W(20, g);
        const int32_t xh = (w0 >> 12) & 0xFFF, yh = w0 & 0xFFF, xl = (w1 >> 12) & 0xFFF, yl = w1 & 0xFFF;
        int32_t s = (int16_t)(st >> 16), t = (int16_t)(st & 0xFFFF);
        const int32_t dsdx = (int16_t)(dd >> 16), dtdy = (int16_t)(dd & 0xFFFF);
        const int32_t nxl = std::max(xl, d.cx0), nyl = std::max(yl, d.cy0);
        const int32_t nxh = std::min(xh, d.cx1), nyh = std::min(yh, d.cy1);
        if (nxl >= nxh || nyl >= nyh) {
            for (int i = 0; i < 3; i++) {  // G_SPNOOP
                MEM_W(i * 8, g) = 0;
                MEM_W(i * 8 + 4, g) = 0;
            }
        } else {
            // (pixels / 4) * (texels per pixel / 1024) * 32 = 10.2 pixels * dsdx / 128.
            s += ((nxl - xl) * dsdx + 64) >> 7;
            t += ((nyl - yl) * dtdy + 64) >> 7;
            MEM_W(0, g) = (int32_t)(0xE4000000u | ((uint32_t)nxh << 12) | (uint32_t)nyh);
            MEM_W(4, g) = (int32_t)((w1 & 0x07000000u) | ((uint32_t)nxl << 12) | (uint32_t)nyl);
            MEM_W(12, g) = (int32_t)(((uint32_t)(s & 0xFFFF) << 16) | (uint32_t)(t & 0xFFFF));
        }
        g += 16;
    }
    if (d.title_card) {
        ctx->r2 = draw_title_branding(rdram, ctx->r2, d.ox, d.oy, d.sx, d.sy);
    }
}

// Draw distance. The field keeps a 3x3 block of map cells loaded around the player (100 units each outdoors; world
// space has its origin at the centre cell's corner) and draws them with a far clip of 180 units (RenderZones,
// aidyn.us.toml hook), so with the fog at the far end of the depth range the loaded ground ends 100 to 280 units away.
// settings.json draw_distance (1..3) is used three ways:
// - The far clip follows the loaded ground: each frame it goes to where the view (the whole picture's width) leaves
//   the 3x3 block, never under the original 180 nor over 180 x draw_distance, eased so it doesn't jump when turning.
//   The game's fog still thickens into it, so the edge of the loaded world never shows.
// - Props (trees, rocks, objects: RenderVoxelScenes) measure their distance from the player as 1/draw_distance of it
//   (aidyn.us.toml hook after its Vec3Dist), so their cut-off and level-of-detail bands move out together.
// - The views the game culls with were made for 4:3: map cells outside a 42.5-degree wedge 200 units long
//   (func_8000D270: 0x800CD6F4 length, 0x800CD6F8 margin, 0x800CD6FC angle) and props outside a cone fading from
//   37.5 to 42.5 degrees (RenderVoxelScenes: 0x800CD6A0, 0x800CD6A4, radians). Each constant is read only there; they
//   are rewritten each frame for the picture's width (the field's vertical field of view is 45 degrees) and the far
//   clip, so nothing is cut at the sides of a 16:9 or 21:9 picture.
namespace {
    constexpr int32_t kGlobals = 0x800D8088;
    constexpr float kPi = 3.14159265f;

    float view_aspect() {
        const auto& gfx = aidyn::settings::graphics();
        switch (gfx.aspect) {
            case aidyn::settings::Aspect::Ratio: return (float)gfx.aspect_ratio;
            case aidyn::settings::Aspect::Window: return 2.4f;  // assume an ultrawide window
            default: return 4.0f / 3.0f;
        }
    }

    // Half the horizontal field of view, degrees.
    float half_view_degrees() {
        return std::atan(std::tan(22.5f * kPi / 180.0f) * view_aspect()) * 180.0f / kPi;
    }

    int far_ring_radius();  // below, "Far map cells": 1 = only the game's 3x3 block
    float far_clip_now = 180.0f;  // the field's far clip this frame (aidyn_field_far_plane)

    // Depth (along the view) at which the picture first leaves the loaded block of cells.
    float loaded_edge_depth(uint8_t* rdram) {
        const gpr g = (gpr)kGlobals;
        const float px = read_f32(rdram, g + 0x214), pz = read_f32(rdram, g + 0x21C);  // gCamera.pos
        const float ax = read_f32(rdram, g + 0x220), az = read_f32(rdram, g + 0x228);  // gCamera.aim
        const float cx = read_f32(rdram, (gpr)(int32_t)0x800D8404), cz = read_f32(rdram, (gpr)(int32_t)0x800D8408);
        float fx = ax - px, fz = az - pz;
        const float len = std::sqrt(fx * fx + fz * fz);
        if (len < 1e-4f || cx <= 0.0f || cz <= 0.0f) {
            return 1e9f;
        }
        fx /= len;
        fz /= len;
        const float half = half_view_degrees() * kPi / 180.0f;
        const float r = (float)far_ring_radius();
        float depth = 1e9f;
        for (int k = -4; k <= 4; k++) {
            const float a = half * (float)k / 4.0f, c = std::cos(a), sn = std::sin(a);
            const float dx = fx * c - fz * sn, dz = fx * sn + fz * c;
            float t = 1e9f;
            if (dx > 1e-5f) t = std::min(t, ((r + 1.0f) * cx - px) / dx);
            if (dx < -1e-5f) t = std::min(t, (-r * cx - px) / dx);
            if (dz > 1e-5f) t = std::min(t, ((r + 1.0f) * cz - pz) / dz);
            if (dz < -1e-5f) t = std::min(t, (-r * cz - pz) / dz);
            depth = std::min(depth, std::max(t, 0.0f) * c);
        }
        return depth;
    }

    void write_f32_at(uint8_t* rdram, int32_t address, float f) {
        write_f32(rdram, (gpr)address, f);
    }
}

extern "C" void get_memFree(uint8_t* rdram, recomp_context* ctx);  // the game's free heap bytes (0x8008E980)

// AIDYN_FAR_LOG also counts, per second, the props tried and how many are near enough to draw by the original rule
// and by the scaled one.
static int props_tried = 0, props_original = 0, props_scaled = 0;

// AIDYN_FAR_LOG=1: once a second, the far clip, the loaded ground's edge and the game's free heap.
static void log_far(uint8_t* rdram, recomp_context* ctx, float far, float edge) {
    static const bool enabled = std::getenv("AIDYN_FAR_LOG") != nullptr;
    static int frames = 0;
    if (!enabled || ++frames % 60 != 0) {
        return;
    }
    recomp_context saved = *ctx;  // mid-function: every register is someone's
    get_memFree(rdram, ctx);
    const uint32_t free_bytes = (uint32_t)ctx->r2;
    *ctx = saved;
    fprintf(stdout, "far %.0f edge %.0f free heap %u KB, props a frame: tried %d, near enough %d originally, %d now\n",
            far, std::min(edge, 9999.0f), free_bytes / 1024, props_tried / 60, props_original / 60, props_scaled / 60);
    props_tried = props_original = props_scaled = 0;
    fflush(stdout);
}

extern "C" uint32_t aidyn_field_far_plane(uint8_t* rdram, recomp_context* ctx, uint32_t far_bits) {
    float original;
    std::memcpy(&original, &far_bits, sizeof(original));
    const float scale = aidyn::settings::graphics().draw_distance;
    const float edge = loaded_edge_depth(rdram);
    const float target = std::clamp(edge, original, original * scale);
    static float current = 0.0f;
    current = current <= 0.0f ? target : current + (target - current) * 0.12f;
    const float far = std::clamp(current, original, original * scale);
    far_clip_now = far;
    const float half = half_view_degrees();
    write_f32_at(rdram, 0x800CD6F4, far * (200.0f / 180.0f));                  // cell wedge length (200 at 180)
    write_f32_at(rdram, 0x800CD6FC, std::max(42.5f, half + 13.6f));            // cell wedge angle, degrees
    write_f32_at(rdram, 0x800CD6A0, std::max(37.5f, half + 8.6f) * kPi / 180.0f);   // props: fading from
    write_f32_at(rdram, 0x800CD6A4, std::max(42.5f, half + 13.6f) * kPi / 180.0f);  // props: gone at
    log_far(rdram, ctx, far, edge);
    uint32_t bits;
    std::memcpy(&bits, &far, sizeof(far));
    return bits;
}

extern "C" void aidyn_prop_distance(uint8_t* rdram, recomp_context* ctx) {
    const float scale = aidyn::settings::graphics().draw_distance;
    float prox;
    uint32_t bits = ctx->f0.u32l;
    std::memcpy(&prox, &bits, sizeof(prox));
    const float size = read_f32(rdram, ctx->r19 + 0xC);  // the prop's cut-off distance (s3 = the prop)
    props_tried++;
    props_original += prox < size;
    prox /= scale;
    props_scaled += prox < size;
    std::memcpy(&bits, &prox, sizeof(prox));
    ctx->f0.u32l = bits;
}

// RenderVoxelScenes puts props that are fading (or drawn see-through) in a 32-entry table drawn after the rest
// (0x800E6710, count at 0x800DA38E), without checking it is full. With props drawn farther more can fade at once; when
// it is full the prop is drawn at once instead (aidyn.us.toml hook where an entry is taken).
extern "C" int aidyn_prop_table_full(uint8_t* rdram) {
    return MEM_HU(0, (gpr)(int32_t)0x800DA38E) >= 32;
}

// Far map cells. Outdoors the game keeps only the 3x3 block of map cells around the player, so the ground ends 100 to
// 280 units away. With settings.json draw_distance above 1, rings of cells around that block are loaded and drawn too
// (their ground only: no props, people or collision), out to radius R (5x5 at R = 2 ... 9x9 at R = 4, so the far
// clip, which follows the loaded edge, can reach 180 x draw_distance):
// - Each frame (aidyn.us.toml hook in RenderZones after the game's own cells, display list at sp+0x64): cells that left
//   the rings or became part of the 3x3 are freed through the game's free queue; missing ring cells are loaded, two a
//   frame, nearest first, with the game's BorgAnimLoadScene, while its heap's largest free block stays over 512 KB;
//   each is placed as the game places its neighbours (ZoneMoveSceneDat: flags 0x10, fog, 0x8, 0x4, identity matrices,
//   matrixA at (dx, 0, dz) cells from the centre cell's origin) and drawn, when in view, with SetSceneColors +
//   BorgAnimDrawScene, as the game draws a cell.
// - A ring cell is skipped when the 3x3 neighbour between it and the player isn't loaded: the game leaves some
//   neighbours out on purpose (the centre cell's map hides them), and what lies past them wasn't meant to be seen.
// - The game is told it has 15 MB of RAM, not 8 (aidyn.us.toml hook in MemoryCheck): its heap grows from 6.2 to 13 MB
//   and its framebuffers move up to just under 15 MB. Nothing in the game assumes 8 MB; RT64 sees 16 MB of addresses.
// - FreeZoneEngine and FreeZoneEngineMemory (leaving the field) free every ring cell.
// The ring cells' scene pointers live in RDRAM (kFarTable) so the game's free queue can clear them.
extern "C" void BorgAnimLoadScene(uint8_t* rdram, recomp_context* ctx);
extern "C" void BorgAnimDrawScene(uint8_t* rdram, recomp_context* ctx);
extern "C" void SetSceneColors(uint8_t* rdram, recomp_context* ctx);
extern "C" void get_memFree_2(uint8_t* rdram, recomp_context* ctx);
extern "C" void AllocFreeQueueItem(uint8_t* rdram, recomp_context* ctx);

namespace {
    constexpr uint32_t kGameMemory = 15u << 20;
    constexpr int32_t kFarTable = 0x80F10000;     // RDRAM, above the branding pictures
    constexpr int32_t kQueueA = 0x800D96B0;       // gGlobals.QueueA, the free queue
    constexpr int32_t kMapIndices = 0x800DCBD0;   // gLoadedMapIndecies[22][30][3]: borg5, borg9, borg5 ALT
    constexpr int32_t kZoneDatMtx = 0x800D8168;   // ZoneDat[3][3], 0x20 each, rows of 0x60
    constexpr int kMaxRing = 4, kSide = 2 * kMaxRing + 1;

    struct FarCell {
        int gx = 0, gy = 0;       // map grid cell
        uint16_t borg5 = 0;
        bool used = false;
    };
    FarCell far_cells[kSide * kSide];  // slot k's scene pointer is at kFarTable + 4k
    int far_center_x = -1000, far_center_y = -1000;
    int far_radius_now = 1;

    uint32_t call_game(uint8_t* rdram, recomp_context* ctx, void (*fn)(uint8_t*, recomp_context*),
                       uint32_t a0 = 0, uint32_t a1 = 0, uint32_t a2 = 0, uint32_t a3 = 0) {
        recomp_context saved = *ctx;  // mid-function in the game: every register is someone's
        ctx->r4 = (int32_t)a0;
        ctx->r5 = (int32_t)a1;
        ctx->r6 = (int32_t)a2;
        ctx->r7 = (int32_t)a3;
        fn(rdram, ctx);
        const uint32_t v0 = (uint32_t)ctx->r2;
        *ctx = saved;
        return v0;
    }

    gpr far_slot(int k) {
        return (gpr)(kFarTable + 4 * k);
    }

    bool outdoors(uint8_t* rdram) {
        return MEM_HU(0, (gpr)(int32_t)0x800D8414) == 0  // gGlobals.gameVars.mapDatA: MAPA_Overworld
               && read_f32(rdram, (gpr)(int32_t)0x800D8404) == 100.0f;
    }

    int wanted_radius() {
        const float dd = aidyn::settings::graphics().draw_distance;
        if (dd <= 1.0f) {
            return 1;
        }
        return std::clamp((int)std::ceil(180.0f * dd / 100.0f), 2, kMaxRing);
    }

    uint16_t map_index(uint8_t* rdram, int gx, int gy, int k) {
        if (gx < 1 || gx > 21 || gy < 1 || gy > 29) {
            return 0;
        }
        return MEM_HU(((gx * 30 + gy) * 3 + k) * 2, (gpr)kMapIndices);
    }

    void free_far_cell(uint8_t* rdram, recomp_context* ctx, int k) {
        if (!far_cells[k].used) {
            return;
        }
        far_cells[k].used = false;
        const uint32_t scene = (uint32_t)MEM_W(0, far_slot(k));
        if (scene == 0) {
            return;
        }
        // Through the game's free queue, which frees at the start of the next frame (the renderer may still be
        // reading this frame's display list) and clears the slot now.
        call_game(rdram, ctx, AllocFreeQueueItem, (uint32_t)kQueueA, (uint32_t)far_slot(k), 1 /* QueueType_Scene */, 0);
    }

    void place_far_cell(uint8_t* rdram, int k, int dx, int dz) {
        const gpr scene = (gpr)(int32_t)MEM_W(0, far_slot(k));
        const float cx = read_f32(rdram, (gpr)(int32_t)0x800D8404), cz = read_f32(rdram, (gpr)(int32_t)0x800D8408);
        MEM_W(0x1F4, scene) = 0x10 | 0x100 | 0x8 | 0x4;  // ZoneMoveSceneDat: SetFlag10, SetFogFlag, SetFlag8, SetFlag4
        for (int m : { 0x88, 0xC8 }) {                    // matrixA, matrixB = identity
            for (int i = 0; i < 16; i++) {
                write_f32(rdram, scene + m + i * 4, (i % 5 == 0) ? 1.0f : 0.0f);
            }
        }
        write_f32(rdram, scene + 0x88 + 48, dx * cx);     // MatrixASetPos: matrixA[3][0..2]
        write_f32(rdram, scene + 0x88 + 56, dz * cz);
    }

    // In view: some corner of the cell within the picture's width (plus a margin) and the far clip.
    bool far_cell_visible(uint8_t* rdram, int dx, int dz, float far) {
        const gpr g = (gpr)kGlobals;
        const float px = read_f32(rdram, g + 0x214), pz = read_f32(rdram, g + 0x21C);
        float fx = read_f32(rdram, g + 0x220) - px, fz = read_f32(rdram, g + 0x228) - pz;
        const float len = std::sqrt(fx * fx + fz * fz);
        if (len < 1e-4f) {
            return true;
        }
        fx /= len;
        fz /= len;
        const float cx = read_f32(rdram, (gpr)(int32_t)0x800D8404), cz = read_f32(rdram, (gpr)(int32_t)0x800D8408);
        const float cosv = std::cos((half_view_degrees() + 15.0f) * kPi / 180.0f);
        for (int i = 0; i <= 2; i++) {
            for (int j = 0; j <= 2; j++) {  // corners and edge midpoints
                const float x = (dx + i * 0.5f) * cx - px, z = (dz + j * 0.5f) * cz - pz;
                const float d = std::sqrt(x * x + z * z);
                if (d < 1.0f) {
                    return true;
                }
                if ((x * fx + z * fz) / d >= cosv && d * ((x * fx + z * fz) / d) <= far + 10.0f) {
                    return true;
                }
            }
        }
        return false;
    }

}

namespace {
    int far_ring_radius() {
        return far_radius_now;
    }
}

extern "C" uint32_t aidyn_game_memory(uint32_t reported) {
    return std::max(reported, kGameMemory);
}

extern "C" void aidyn_far_cells_free_all(uint8_t* rdram, recomp_context* ctx) {
    for (int k = 0; k < kSide * kSide; k++) {
        free_far_cell(rdram, ctx, k);
    }
    far_center_x = far_center_y = -1000;
    far_radius_now = 1;
}

extern "C" void aidyn_far_cells(uint8_t* rdram, recomp_context* ctx) {
    const int radius = outdoors(rdram) ? wanted_radius() : 1;
    const int sx = MEM_H(0, (gpr)(int32_t)0x800D83FA), sy = MEM_H(0, (gpr)(int32_t)0x800D83FC);  // mapShort1/2
    // Free cells no longer in the rings (or now in the game's 3x3).
    for (int k = 0; k < kSide * kSide; k++) {
        if (!far_cells[k].used) {
            continue;
        }
        const int dx = far_cells[k].gx - sx, dz = far_cells[k].gy - sy;
        if (std::abs(dx) > radius || std::abs(dz) > radius || (std::abs(dx) <= 1 && std::abs(dz) <= 1)) {
            free_far_cell(rdram, ctx, k);
        }
    }
    const bool moved = sx != far_center_x || sy != far_center_y;
    far_center_x = sx;
    far_center_y = sy;
    if (radius <= 1) {
        far_radius_now = 1;
        return;
    }
    // Place the loaded ones again after the centre moved, and find the missing ones, nearest first.
    struct Want { int gx, gy, dx, dz; };
    Want missing[kSide * kSide];
    int n_missing = 0;
    bool complete = true;
    for (int dz = -radius; dz <= radius; dz++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (std::abs(dx) <= 1 && std::abs(dz) <= 1) {
                continue;
            }
            const int gx = sx + dx, gy = sy + dz;
            const uint16_t borg5 = map_index(rdram, gx, gy, 0);
            if (borg5 == 0) {
                continue;
            }
            // The 3x3 neighbour toward it must be loaded (the game hides some on purpose).
            const int ni = std::clamp(dx, -1, 1) + 1, nj = std::clamp(dz, -1, 1) + 1;
            if (MEM_W(4, (gpr)(kZoneDatMtx + ni * 0x60 + nj * 0x20)) == 0) {
                continue;
            }
            int found = -1;
            for (int k = 0; k < kSide * kSide; k++) {
                if (far_cells[k].used && far_cells[k].gx == gx && far_cells[k].gy == gy) {
                    found = k;
                    break;
                }
            }
            if (found >= 0) {
                if (moved) {
                    place_far_cell(rdram, found, dx, dz);
                }
            } else {
                missing[n_missing++] = { gx, gy, dx, dz };
                complete = false;
            }
        }
    }
    far_radius_now = complete ? radius : std::max(1, radius - 1);
    std::sort(missing, missing + n_missing, [](const Want& a, const Want& b) {
        return std::max(std::abs(a.dx), std::abs(a.dz)) * 100 + std::abs(a.dx) + std::abs(a.dz) <
               std::max(std::abs(b.dx), std::abs(b.dz)) * 100 + std::abs(b.dx) + std::abs(b.dz);
    });
    for (int i = 0, loads = 0; i < n_missing && loads < 2; i++) {
        if (call_game(rdram, ctx, get_memFree_2) < 0x80000) {
            break;  // keep room for the game's own loads
        }
        int k = 0;
        while (k < kSide * kSide && far_cells[k].used) {
            k++;
        }
        if (k == kSide * kSide || MEM_W(0, far_slot(k)) != 0) {
            break;  // (a slot still waiting in the free queue)
        }
        const uint16_t borg5 = map_index(rdram, missing[i].gx, missing[i].gy, 0);
        const uint32_t scene = call_game(rdram, ctx, BorgAnimLoadScene, borg5);
        if (scene == 0) {
            break;
        }
        MEM_W(0, far_slot(k)) = (int32_t)scene;
        far_cells[k] = { missing[i].gx, missing[i].gy, borg5, true };
        place_far_cell(rdram, k, missing[i].dx, missing[i].dz);
        loads++;
    }
    // Draw the ones in view, after the game's own cells (display list pointer at sp+0x64 in RenderZones).
    const gpr sp = ctx->r29;
    uint32_t g = (uint32_t)MEM_W(0x64, sp);
    for (int k = 0; k < kSide * kSide; k++) {
        if (!far_cells[k].used || MEM_W(0, far_slot(k)) == 0) {
            continue;
        }
        const int dx = far_cells[k].gx - sx, dz = far_cells[k].gy - sy;
        if (!far_cell_visible(rdram, dx, dz, far_clip_now)) {
            continue;
        }
        const uint32_t scene = (uint32_t)MEM_W(0, far_slot(k));
        call_game(rdram, ctx, SetSceneColors, scene, 0xFF, 1, 0);
        g = call_game(rdram, ctx, BorgAnimDrawScene, g, scene);
    }
    MEM_W(0x64, sp) = (int32_t)g;
}

// Widescreen clouds. Clouds::Draw positions each cloud (a 2D image) from the 3D camera, so in 16:9 some belong outside
// the 4:3 picture, but the game's scissor (0..320) and gSPScisTextureRectangle's clamp at x = 0 cut them off at the
// 4:3 edges. Three aidyn.us.toml hooks: on entry to the outdoor branch, aidyn_clouds_begin writes RT64 extended-GBI
// commands into the display list (scissor to the full window; every rectangle moved left by kCloudShift);
// before each cloud's Borg8_DrawSimple, aidyn_cloud_x moves the cloud right by kCloudShift (so its left edge is never
// below 0 and nothing gets clamped); on exit, aidyn_clouds_end restores the scissor and the rectangle offset.
#define F3DEX_GBI_2
#include "rt64_extended_gbi.h"

namespace {
    // Texels. At 21:9 a cloud (up to 173 wide) can start ~293 left of the 4:3 edge; rectangle x stays under 1024 (12-bit
    // 10.2 coordinates), which leaves room up to 639 on the right (4:3 ends at 320).
    constexpr int kCloudShift = 384;
    bool clouds_open = false;

    gpr put_cmds(uint8_t* rdram, gpr g, const GfxCommand* cmds, int n) {
        for (int i = 0; i < n; i++) {
            MEM_W(0, g) = (int32_t)cmds[i].values.word0;
            MEM_W(4, g) = (int32_t)cmds[i].values.word1;
            g += 8;
        }
        return g;
    }
}

extern "C" gpr aidyn_clouds_begin(uint8_t* rdram, gpr g) {
    if (aidyn::settings::graphics().aspect == aidyn::settings::Aspect::Original) {
        return g;
    }
    GfxCommand c[5];
    gEXEnable(&c[0]);
    gEXSetScissor(&c[1], 0 /* G_SC_NON_INTERLACE */, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, 0, 0, 240);
    gEXSetRectAlign(&c[3], G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, -kCloudShift * 4, 0, -kCloudShift * 4, 0);
    clouds_open = true;
    return put_cmds(rdram, g, c, 5);
}

extern "C" void aidyn_cloud_x(recomp_context* ctx) {
    if (!clouds_open) {
        return;
    }
    uint32_t bits = (uint32_t)ctx->r6;  // Borg8_DrawSimple's x (a float, passed in a2)
    float x;
    std::memcpy(&x, &bits, sizeof(x));
    x += kCloudShift;
    std::memcpy(&bits, &x, sizeof(x));
    ctx->r6 = (gpr)(int32_t)bits;
}

extern "C" gpr aidyn_clouds_end(uint8_t* rdram, gpr g) {
    if (!clouds_open) {
        return g;
    }
    clouds_open = false;
    const uint32_t h = MEM_HU(kHres1, (gpr)gGfxManager);
    const uint32_t v = MEM_HU(kVres1, (gpr)gGfxManager);
    GfxCommand c[3];
    gEXSetRectAlign(&c[0], G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0);
    c[2].values.word0 = 0xED000000;                                   // gDPSetScissor(G_SC_NON_INTERLACE, 0, 0, h, v)
    c[2].values.word1 = ((h * 4) & 0xFFF) << 12 | ((v * 4) & 0xFFF);
    return put_cmds(rdram, g, c, 3);
}

// Font::GetButtonColor(u8 button, u8* r, u8* g, u8* b) (func_800AA2B0, ignored in aidyn.us.toml): the tint of the
// controller-button icons in text (A blue, B green, C yellow, Start red, the rest grey; table D_800E4B2C, 3 bytes
// each). With the HD UI pack the icons are drawn as full-colour Xbox buttons (tools/hdui/buttons.py), so the tint is
// white; without it, the game's colours.
#include <algorithm>

extern "C" void func_800AA2B0(uint8_t* rdram, recomp_context* ctx) {
    const uint32_t button = (uint32_t)ctx->r4 & 0xFF;
    const auto& packs = aidyn::settings::graphics().texture_packs;
    const bool xbox = std::find(packs.begin(), packs.end(), "hd-ui") != packs.end();
    for (int i = 0; i < 3; i++) {
        const gpr out = i == 0 ? ctx->r5 : (i == 1 ? ctx->r6 : ctx->r7);
        MEM_B(0, out) = xbox ? 0xFF : MEM_BU(button * 3 + i, (gpr)(int32_t)0x800E4B2C);
    }
}

// Frame interpolation. Above 60 frames a second (settings.json fps_limit) RT64 draws in-between frames by pairing each
// matrix of one game frame with one of the next and interpolating. Left to itself it pairs them by guesswork, and an
// occasional wrong pair stretched a polygon across the screen for a frame. Every model node the game draws goes through
// BorgAnimDrawSceneRaw, which loads the node's world matrix with gSPMatrix; aidyn.us.toml hooks it so each node's
// matrix is tagged with an RT64 matrix group whose ID is the scene and node (stable while the model is loaded), matched
// exactly between frames. On entry the extended GBI is switched on (RT64 switches it off after every frame); before
// each node's matrix the group (2 commands, the display list has room: it peaks around 6400 of its 25600); on return,
// RT64's automatic group again for whatever is drawn after.
namespace {
    bool interpolating() {
        const int limit = aidyn::settings::graphics().fps_limit;
        return limit == 0 || limit > 60;
    }
}

extern "C" void aidyn_scene_draw_begin(uint8_t* rdram, recomp_context* ctx) {
    if (!interpolating()) {
        return;
    }
    GfxCommand c[1];
    gEXEnable(&c[0]);
    ctx->r4 = put_cmds(rdram, ctx->r4, c, 1);  // a0 = the display list pointer, before the function copies it
}

extern "C" void aidyn_scene_node_matrix(uint8_t* rdram, recomp_context* ctx) {
    if (!interpolating()) {
        return;
    }
    const uint32_t scene = (uint32_t)MEM_W(0, (gpr)(int32_t)0x800E6A40);  // the scene being drawn
    const uint32_t node = (uint32_t)MEM_W(8, (gpr)(int32_t)0x800E6A40);   // its current node
    uint32_t id = (scene * 0x9E3779B1u) ^ node;
    if (id == G_EX_ID_IGNORE || id == G_EX_ID_AUTO) {
        id = 1;
    }
    GfxCommand c[2];
    gEXMatrixGroupDecomposed(&c[0], id, G_EX_NOPUSH, 0, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE,
                             G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_SKIP,
                             G_EX_COMPONENT_AUTO, G_EX_ORDER_LINEAR, G_EX_EDIT_NONE, G_EX_COMPONENT_SKIP,
                             G_EX_COMPONENT_AUTO);
    // s4 = where the gSPMatrix goes, s7 = s4 + 8 (the pointer after it): both move past the group.
    ctx->r20 = put_cmds(rdram, ctx->r20, c, 2);
    ctx->r23 += 16;
}

extern "C" void aidyn_scene_draw_end(uint8_t* rdram, recomp_context* ctx) {
    if (!interpolating()) {
        return;
    }
    // RT64's default group (rt64_transform_group.h): automatic ID, decomposed, components automatic.
    GfxCommand c[2];
    gEXMatrixGroup(&c[0], G_EX_ID_AUTO, G_EX_INTERPOLATE_DECOMPOSE, G_EX_NOPUSH, 0, G_EX_COMPONENT_AUTO,
                   G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO, G_EX_COMPONENT_AUTO,
                   G_EX_COMPONENT_SKIP, G_EX_COMPONENT_AUTO, G_EX_ORDER_AUTO, G_EX_EDIT_NONE, G_EX_ASPECT_AUTO,
                   G_EX_COMPONENT_SKIP, G_EX_COMPONENT_AUTO);
    ctx->r2 = put_cmds(rdram, ctx->r2, c, 2);  // v0 = the display list pointer returned
}

// Animation loops. func_80096570 advances a model's animation one tick (1/60 s): Actor animations call it once per
// tick. When an animation ends it picks the next one (a walk cycle picks itself again: takeBranch 0x800962B4, set up by
// 0x80096A38) and skips Scene::Tick for that tick, so the last pose is shown twice; and the root motion that moves the
// character (+0x2C..+0x34, root position now minus last tick's +0x20) is zeroed that tick and, through the counter
// +0x12, the next two. On the N64, at 15-20 frames a second, that vanished inside one frame; at 60 a walking character
// stopped dead for 50 ms every stride. With settings.json's smooth_animation_loops (default), aidyn.us.toml hooks:
// after the new animation is set up, Scene::Tick runs in the same tick (aidyn_anim_loop_tick); then the motion keeps
// last tick's value instead of being zeroed, with no countdown (aidyn_anim_loop_motion, which skips to the end where
// the new root position is stored).
// AIDYN_ANIM_LOG=1 prints each animation tick's switch flag and z motion, for measuring.
extern "C" void Scene__Tick(uint8_t* rdram, recomp_context* ctx);  // the recompiled Scene::Tick (0x80097098)

// Ticking the new animation at once skips its first frame (aniTime 1) for everything that reads the frame number.
// ProcessPlayers notes which animation an actor is playing for its sounds only on frame 1 (pDat->unk16 = ani_type), and
// player_audiokey plays footsteps, swings, hits and spell sounds on given frames of that animation, so without frame 1
// every sound keyed to an animation was silent. early_switch remembers the models ticked early (Borg7 -> aniTime after
// the tick); aidyn_anim_first_frame (ProcessPlayers' aniTime == 1 check) and aidyn_anim_first_frame_sound (its
// player_audiokey call) give the actor its frame 1 back: the animation is noted, and frame 1's sounds play just before
// frame 2's, in the same tick.
namespace {
    std::unordered_map<uint32_t, uint16_t> early_switch;
    uint32_t first_frame_actor = 0;
}

extern "C" void aidyn_anim_loop_tick(uint8_t* rdram, recomp_context* ctx) {
    if (!aidyn::settings::graphics().smooth_animation_loops) {
        return;
    }
    const gpr borg7 = ctx->r17;
    ctx->r4 = MEM_W(8, borg7);  // the model's scene
    Scene__Tick(rdram, ctx);
    early_switch[(uint32_t)borg7] = MEM_HU(0x1F8, MEM_W(8, borg7));  // SceneData.aniTime
}

// ProcessPlayers before 0x800132A4 (bne a0, v0): s1 = the actor, a0 = its aniTime, v0 = 1. Returns the aniTime to test.
extern "C" gpr aidyn_anim_first_frame(uint8_t* rdram, gpr actor, gpr ani_time) {
    const uint32_t borg7 = (uint32_t)MEM_W(4, actor);
    auto it = early_switch.find(borg7);
    if (it == early_switch.end()) {
        return ani_time;
    }
    const bool now = it->second == (uint16_t)ani_time;
    early_switch.erase(it);
    if (!now) {
        return ani_time;
    }
    first_frame_actor = (uint32_t)actor;
    return 1;
}

extern "C" void player_audiokey(uint8_t* rdram, recomp_context* ctx);  // the recompiled player_audiokey (0x80053560)

// ProcessPlayers before 0x8001341C (jal player_audiokey; its delay slot sets a0 = s1): a1 animation, a2 frame, a3 length.
extern "C" void aidyn_anim_first_frame_sound(uint8_t* rdram, recomp_context* ctx) {
    if (first_frame_actor == 0) {
        return;
    }
    const bool mine = first_frame_actor == (uint32_t)ctx->r17;
    first_frame_actor = 0;
    if (!mine || (uint32_t)(ctx->r6 & 0xFFFF) <= 1) {
        return;
    }
    recomp_context saved = *ctx;
    ctx->r4 = ctx->r17;
    ctx->r6 = 1;
    player_audiokey(rdram, ctx);
    *ctx = saved;
}

extern "C" int aidyn_anim_loop_motion(uint8_t* rdram, gpr anim, int branched) {
    if (!aidyn::settings::graphics().smooth_animation_loops || !branched) {
        return 0;
    }
    MEM_H(0x12, anim) = 0;  // no countdown; aidyn_anim_loop_speed puts last tick's motion back
    return 1;
}

// The two fixes move a walking character on 2 more ticks a cycle and make the cycle a tick shorter (18 ticks for the
// walk, not 19), about 18% faster. To keep the original speed, each animation instance measures its last cycle (a cycle
// ends when it switches animation): D = the motion the fixes give over its n ticks, x = what they added (the switch tick
// and the one after, which the original zeroed), and scales every tick's motion by the original speed over the new one:
// ((D - x) / (n + 1)) / (D / n). Runs at the end of every tick (aidyn.us.toml hook at 0x8009676C).
namespace {
    struct LoopState {
        float raw[3] = { 0.0f, 0.0f, 0.0f };  // last tick's motion before scaling
        float scale = 1.0f;
        float distance = 0.0f, added = 0.0f;
        int ticks = 0;
    };
    std::unordered_map<uint32_t, LoopState> loop_states;
}

extern "C" void aidyn_anim_loop_speed(uint8_t* rdram, gpr anim, int branched) {
    static const bool log = std::getenv("AIDYN_ANIM_LOG") != nullptr;
    if (aidyn::settings::graphics().smooth_animation_loops) {
        LoopState& st = loop_states[(uint32_t)anim];
        float raw[3];
        for (int i = 0; i < 3; i++) {
            raw[i] = branched ? st.raw[i] : read_f32(rdram, anim + 0x2C + i * 4);
        }
        if (branched) {
            if (st.ticks > 2 && st.distance > 0.05f) {
                const float n = (float)st.ticks;
                st.scale = std::clamp((st.distance - st.added) / (n + 1.0f) * n / st.distance, 0.7f, 1.0f);
            }
            st.distance = st.added = 0.0f;
            st.ticks = 0;
        }
        const float step = std::sqrt(raw[0] * raw[0] + raw[2] * raw[2]);
        st.distance += step;
        if (st.ticks < 2) {
            st.added += step;
        }
        st.ticks++;
        for (int i = 0; i < 3; i++) {
            st.raw[i] = raw[i];
            write_f32(rdram, anim + 0x2C + i * 4, raw[i] * st.scale);
        }
    }
    if (log) {
        fprintf(stdout, "anim %08X branch %d countdown %d motion z %.4f\n", (uint32_t)anim, branched,
                (int)MEM_H(0x12, anim), read_f32(rdram, anim + 0x34));
    }
}

// Startup logos. load_splashscreen_data (TitleSplash::Load) starts the boot sequence at state 5 (gGlobals.
// titleSplashVars.state, 0x800D80A8): Expansion Pak notice, copyright, Nintendo logo, THQ, H2O, then 0 = done, on to
// the title screen. With settings.json game.skip_intro (default) an aidyn.us.toml hook at its return sets 0.
//
// The intro music starts with the logos (load_intro_music) and plays on into the title screen, so with the logos
// skipped the title screen opened on the part written for them. It is moved on to where it was when the title screen
// used to appear (aidyn_dcm_tick).
namespace {
    std::atomic_bool intro_seek{ false };
}

extern "C" void aidyn_splash_loaded(uint8_t* rdram) {
    if (aidyn::settings::game().skip_intro) {
        MEM_B(0, (gpr)(int32_t)0x800D80A8) = 0;
        intro_seek = true;
    }
}

// The game's music player (decomp dcm.cpp, DCM::VoiceHandler 0x80090C94, run by the audio thread once per tick) plays
// up to 32 tracks (gDCMManager 0x800E41F0: DCM_struct* at +0x14, 0x20 each: channel states +0x00, Borg12Sub* +0x04,
// end tick +0x08, ticks played +0x0C, read offset +0x10, id +0x14, active +0x18 (2: playing), tempo divider +0x19 and
// its count +0x1A, loops +0x1D). A track is one command stream for all its channels (Borg12Sub: channel count low byte
// at +0x03, length +0x08, loop offset +0x0C, data +0x14); each channel (0x10 bytes: voice id +0x00, unk4 +0x04, pitch
// +0x08, voice +0x0A, instrument +0x0B, volume +0x0C, pan +0x0D, wait +0x0E) reads its next command when its wait is
// 0. DcmTrack is a copy of one track's state, and tick() is one handler call on it with the sound left out: reading
// the stream ahead moves the music on without playing it.
namespace {
    constexpr uint32_t kDcmManager = 0x800E41F0;
    constexpr uint32_t kIntroIndex = 0x800D80A0, kIntroId = 0x800D80A4, kSplashState = 0x800D80A8;
    // Ticks of the intro music played before the title screen appears when the logos run: measured with
    // AIDYN_INTRO_LOG=1 and game.skip_intro false.
    constexpr uint32_t kIntroTitleTick = 12947;

    struct DcmTrack {
        struct Channel {
            uint32_t unk4 = 0;
            uint16_t pitch = 0;
            uint8_t instrument = 0, volume = 0, pan = 0, wait = 0;
        };
        uint32_t played = 0, offset = 0;
        uint8_t divider = 0, count = 0;
        Channel ch[32];
        bool ended = false;

        static gpr address(uint8_t* rdram, uint32_t index) {
            return (gpr)(int32_t)((uint32_t)MEM_W(0x14, (gpr)(int32_t)kDcmManager) + index * 0x20);
        }
        void load(uint8_t* rdram, gpr t) {
            played = (uint32_t)MEM_W(0xC, t);
            offset = (uint32_t)MEM_W(0x10, t);
            divider = MEM_BU(0x19, t);
            count = MEM_BU(0x1A, t);
            const gpr c = MEM_W(0, t);
            for (uint32_t i = 0; i < 32; i++) {
                Channel& k = ch[i];
                k.unk4 = (uint32_t)MEM_W(4 + i * 0x10, c);
                k.pitch = (uint16_t)MEM_HU(8 + i * 0x10, c);
                k.instrument = MEM_BU(0xB + i * 0x10, c);
                k.volume = MEM_BU(0xC + i * 0x10, c);
                k.pan = MEM_BU(0xD + i * 0x10, c);
                k.wait = MEM_BU(0xE + i * 0x10, c);
            }
        }
        void store(uint8_t* rdram, gpr t) const {
            MEM_W(0xC, t) = (int32_t)played;
            MEM_W(0x10, t) = (int32_t)offset;
            MEM_B(0x19, t) = divider;
            MEM_B(0x1A, t) = count;
            const gpr c = MEM_W(0, t);
            for (uint32_t i = 0; i < 32; i++) {
                const Channel& k = ch[i];
                MEM_W(4 + i * 0x10, c) = (int32_t)k.unk4;
                MEM_H(8 + i * 0x10, c) = (int16_t)k.pitch;
                MEM_B(0xB + i * 0x10, c) = k.instrument;
                MEM_B(0xC + i * 0x10, c) = k.volume;
                MEM_B(0xD + i * 0x10, c) = k.pan;
                MEM_B(0xE + i * 0x10, c) = k.wait;
            }
        }
        bool same(const DcmTrack& o, uint32_t channels) const {
            if (played != o.played || offset != o.offset || divider != o.divider || count != o.count) {
                return false;
            }
            for (uint32_t i = 0; i < channels; i++) {
                const Channel &a = ch[i], &b = o.ch[i];
                if (a.unk4 != b.unk4 || a.pitch != b.pitch || a.instrument != b.instrument || a.volume != b.volume
                    || a.pan != b.pan || a.wait != b.wait) {
                    return false;
                }
            }
            return true;
        }
        void tick(uint8_t* rdram, gpr sub, uint8_t loops) {
            const uint32_t channels = MEM_BU(3, sub) & 31, length = (uint32_t)MEM_W(8, sub);
            const gpr data = MEM_W(0x14, sub);
            auto byte = [&]() { return (uint8_t)MEM_BU(offset++, data); };
            if (count < divider) {
                count++;
            }
            else {
                for (uint32_t i = 0; i < channels; i++) {
                    Channel& k = ch[i];
                    if (k.wait != 0) {
                        k.wait--;
                        continue;
                    }
                    const uint8_t cmd = byte();
                    if (cmd & 0x80) {
                        k.wait = (cmd & 0x7F) + 1;
                        continue;
                    }
                    if (cmd & 0x40) {
                        const uint16_t lo = byte(), hi = byte(), raw = lo | (hi << 8), shift = raw >> 14;
                        static constexpr uint16_t base[4] = { 0, 0x4000, 0xC000, 0xC000 };
                        k.pitch = (uint16_t)(base[shift] + ((raw & 0x3FFF) << shift));
                    }
                    if (cmd & 0x20) {
                        k.volume = (uint8_t)std::sqrt((float)byte());
                    }
                    if (cmd & 0x10) {
                        k.pan = byte();
                    }
                    if (cmd & 0x08) {
                        k.instrument = byte();
                    }
                    k.unk4 = (cmd & 0x04) ? (uint32_t)byte() << 8 : 0;
                    if (cmd & 0x01) {
                        const uint8_t sub_cmd = byte();
                        if (sub_cmd & 0x40) {
                            uint8_t t = byte();
                            if (t != 0) {
                                t = (uint8_t)(125000u / ((uint32_t)t * 50u)) - 1;
                            }
                            divider = t;
                        }
                        if (sub_cmd & 0x20) {
                            offset++;
                        }
                    }
                }
                if (offset >= length) {
                    if (loops == 0) {
                        ended = true;
                    }
                    offset = (uint32_t)MEM_W(0xC, sub);
                    for (Channel& k : ch) {
                        k.wait = 0;
                    }
                }
                count = 0;
            }
            played++;
        }
    };
    DcmTrack intro_start;  // AIDYN_INTRO_LOG: the intro music's state on its first tick
    bool intro_start_seen = false, intro_logged = false;
}

extern "C" void func_8008FAB0(uint8_t* rdram, recomp_context* ctx);  // the recompiled DCM::StopVoice(voice, id)

// DCM::VoiceHandler's entry (audio thread).
extern "C" void aidyn_dcm_tick(uint8_t* rdram, recomp_context* ctx) {
    static const bool log = std::getenv("AIDYN_INTRO_LOG") != nullptr;
    if (!log && !intro_seek) {
        return;
    }
    const uint32_t index = (uint32_t)MEM_W(0, (gpr)(int32_t)kIntroIndex), id = (uint32_t)MEM_W(0, (gpr)(int32_t)kIntroId);
    if (index >= 32) {
        return;
    }
    const gpr t = DcmTrack::address(rdram, index);
    if (MEM_BU(0x18, t) != 2 || (uint32_t)MEM_W(0x14, t) != id) {
        return;  // not playing (yet)
    }
    const gpr sub = MEM_W(4, t);
    if (log) {
        if (!intro_start_seen && MEM_W(0xC, t) == 0) {
            intro_start.load(rdram, t);
            intro_start_seen = true;
        }
        if (intro_start_seen && !intro_logged && MEM_BU(0, (gpr)(int32_t)kSplashState) == 0) {
            intro_logged = true;
            DcmTrack now, sim = intro_start;
            now.load(rdram, t);
            while (sim.played < now.played) {
                sim.tick(rdram, sub, MEM_BU(0x1D, t));
            }
            fprintf(stderr, "intro music: title screen at tick %u (offset %u); reading ahead %s\n", now.played,
                    now.offset, sim.same(now, MEM_BU(3, sub) & 31) ? "matches" : "DIFFERS");
        }
    }
    if (intro_seek.exchange(false)) {
        DcmTrack track;
        track.load(rdram, t);
        while (track.played < kIntroTitleTick && !track.ended) {
            track.tick(rdram, sub, MEM_BU(0x1D, t));
        }
        track.store(rdram, t);
        const recomp_context saved = *ctx;
        const gpr channels = MEM_W(0, t);
        for (uint32_t i = 0; i < (MEM_BU(3, sub) & 31u); i++) {  // notes started before the jump
            ctx->r4 = MEM_BU(0xA + i * 0x10, channels);
            ctx->r5 = MEM_W(i * 0x10, channels);
            func_8008FAB0(rdram, ctx);
        }
        *ctx = saved;
    }
}

// Title screen. The project is Aidyn ReChronicled: "Rechronicled" goes on the title card under THE FIRST MAGE, and
// "Enhanced by SPITZDANO" at the bottom right. Both are drawn right after the card (aidyn_image_draw_done), with the
// card's colour and fade still set. The pictures (src/game/branding_images.h, generated by tools/branding/
// make_branding.py, which also makes the "rechronicled" texture pack with their full-resolution versions) are copied
// once to RDRAM above the 8 MB the game uses, and each is loaded with one RGBA16 LoadTile and drawn 1:1.
#include "branding_images.h"

namespace {
    constexpr uint32_t kBrandingBase = 0x80F00000;  // RDRAM the game never touches (it is told it has 15 MB, "Far map cells")
    gpr branding_address[std::size(aidyn::branding::kImages)];

    void copy_branding(uint8_t* rdram) {
        static bool done = false;
        if (done) {
            return;
        }
        done = true;
        uint32_t address = kBrandingBase;
        for (size_t i = 0; i < std::size(aidyn::branding::kImages); i++) {
            const auto& img = aidyn::branding::kImages[i];
            branding_address[i] = (gpr)(int32_t)address;
            for (uint32_t k = 0; k < (uint32_t)img.w * img.h; k++) {
                MEM_H(k * 2, (gpr)(int32_t)address) = img.rgba16[k];
            }
            address += ((uint32_t)img.w * img.h * 2 + 7) & ~7u;
        }
    }

    // The image's texel (0, 0) at pixel (x, y), sx and sy pixels a texel.
    gpr draw_rgba16(uint8_t* rdram, gpr g, size_t index, float x, float y, float sx, float sy) {
        const auto& img = aidyn::branding::kImages[index];
        const uint32_t w = img.w, h = img.h, line = (w * 2 + 7) / 8;
        auto cmd = [&](uint32_t w0, uint32_t w1) { MEM_W(0, g) = (int32_t)w0; MEM_W(4, g) = (int32_t)w1; g += 8; };
        const uint32_t clamp = (2u << 18) | (2u << 8);                                  // cmt, cms = G_TX_CLAMP
        cmd(0xE7000000, 0);                                                              // gDPPipeSync
        cmd(0xFD000000 | (0u << 21) | (2u << 19) | (w - 1), (uint32_t)branding_address[index]); // SetTextureImage RGBA16
        cmd(0xF5000000 | (0u << 21) | (2u << 19) | (line << 9), (7u << 24) | clamp);      // SetTile G_TX_LOADTILE
        cmd(0xE6000000, 0);                                                              // gDPLoadSync
        cmd(0xF4000000, (7u << 24) | (((w - 1) << 2) << 12) | ((h - 1) << 2));          // LoadTile
        cmd(0xE7000000, 0);                                                              // gDPPipeSync
        cmd(0xF5000000 | (0u << 21) | (2u << 19) | (line << 9), (0u << 24) | clamp);      // SetTile G_TX_RENDERTILE
        cmd(0xF2000000, (0u << 24) | (((w - 1) << 2) << 12) | ((h - 1) << 2));          // SetTileSize
        const uint32_t xl = (uint32_t)std::max(0, (int)(x * 4.0f)), yl = (uint32_t)std::max(0, (int)(y * 4.0f));
        const uint32_t xh = (uint32_t)(int)((x + w * sx) * 4.0f), yh = (uint32_t)(int)((y + h * sy) * 4.0f);
        const uint32_t dsdx = (uint32_t)(int)(1024.0f / sx) & 0xFFFF, dtdy = (uint32_t)(int)(1024.0f / sy) & 0xFFFF;
        cmd(0xE4000000 | ((xh & 0xFFF) << 12) | (yh & 0xFFF), (0u << 24) | ((xl & 0xFFF) << 12) | (yl & 0xFFF));
        cmd(0xE1000000, 0);                                                              // s, t = 0
        cmd(0xF1000000, (dsdx << 16) | dtdy);
        return g;
    }
}

// The version switch: the running version is aidyn::settings::game().rom_patch; View (N64 C-left) on the title screen
// restarts into the other one (src/main/main.cpp aidyn::switch_version), if its ROM is stored.
#include "version.h"

namespace {
    void title_version_switch() {
        const bool patch = aidyn::settings::game().rom_patch == aidyn::settings::RomPatch::AidynPatch;
        static bool first = true, was_held = false;
        const bool held = aidyn::input::c_left_held();
        if (first) {
            first = false;
            was_held = held;  // still held from the press that switched: wait for the next one
        }
        if (held && !was_held && aidyn::version_available(!patch)) {
            aidyn::switch_version(!patch);
        }
        was_held = held;
    }
}

gpr draw_title_branding(uint8_t* rdram, gpr g, float ox, float oy, float sx, float sy) {
    copy_branding(rdram);
    title_version_switch();
    // "Rechronicled": centred under THE FIRST MAGE (card texels 26..172, rows 120..138) on the card's lower parchment.
    g = draw_rgba16(rdram, g, 0, ox + (97 - 48) * sx, oy + 139 * sy, sx, sy);
    // "Enhanced by SPITZDANO": bottom right of the whole picture, 10 texels in from the edge and the bottom. RT64
    // extended GBI: with the right edge as origin RT64 adds the frame width (320) to x, so the rectangle, drawn where it
    // would sit in 320x240, is moved back 320 by the alignment's offsets (texture rectangles can't take negative x).
    const float hs = read_f32(rdram, (gpr)(int32_t)0x800E49A0), vs = read_f32(rdram, (gpr)(int32_t)0x800E49A4);
    // The game's scissor ends at the 4:3 edge, so it is widened to the whole window meanwhile (as for the clouds).
    if (aidyn::settings::graphics().aspect == aidyn::settings::Aspect::Original) {
        g = draw_rgba16(rdram, g, 1, (310 - 140) * hs, (230 - 14) * vs, hs, vs);
        if (aidyn::version_available(aidyn::settings::game().rom_patch != aidyn::settings::RomPatch::AidynPatch)) {
            g = draw_rgba16(rdram, g, aidyn::settings::game().rom_patch == aidyn::settings::RomPatch::AidynPatch ? 3 : 2,
                            10 * hs, (230 - 12) * vs, hs, vs);
        }
        return g;
    }
    GfxCommand c[5];  // gEXEnable: 1 command; gEXSetScissor, gEXSetRectAlign: 2 each
    gEXEnable(&c[0]);
    gEXSetScissor(&c[1], 0 /* G_SC_NON_INTERLACE */, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, 0, 0, 240);
    gEXSetRectAlign(&c[3], G_EX_ORIGIN_RIGHT, G_EX_ORIGIN_RIGHT, -320 * 4, 0, -320 * 4, 0);
    g = put_cmds(rdram, g, c, 5);
    g = draw_rgba16(rdram, g, 1, (310 - 140) * hs, (230 - 14) * vs, hs, vs);
    // "[View] Original · Aidyn Patch", the running one bright: bottom left, from the left edge of the whole picture.
    const bool patch = aidyn::settings::game().rom_patch == aidyn::settings::RomPatch::AidynPatch;
    const bool both = aidyn::version_available(!patch);
    if (both) {
        gEXSetRectAlign(&c[0], G_EX_ORIGIN_LEFT, G_EX_ORIGIN_LEFT, 0, 0, 0, 0);
        g = put_cmds(rdram, g, c, 2);
        g = draw_rgba16(rdram, g, patch ? 3 : 2, 10 * hs, (230 - 12) * vs, hs, vs);
    }
    const uint32_t h = MEM_HU(kHres1, (gpr)gGfxManager), v = MEM_HU(kVres1, (gpr)gGfxManager);
    gEXSetRectAlign(&c[0], G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0);
    c[2].values.word0 = 0xED000000;                                   // gDPSetScissor(G_SC_NON_INTERLACE, 0, 0, h, v)
    c[2].values.word1 = ((h * 4) & 0xFFF) << 12 | ((v * 4) & 0xFFF);
    g = put_cmds(rdram, g, c, 3);
    return g;
}

// Field HUD at the screen edges. draw_hud_elements draws the time-of-day dial (Sundial::Draw) at the bottom left, the
// party portraits (Portraits::Draw) centred and the compass (Compass::Draw) at the bottom right, in 320x240
// coordinates, which RT64 keeps in the middle 4:3 of a widescreen picture. With settings.json hud_at_edges (default)
// the dial and the compass are drawn with RT64's rectangle alignment set to the picture's left or right edge (aidyn.us.toml hooks on entry, display list
// in a0, and before the return, display list in v0), so they keep their place relative to that edge: the dial 19
// texels from the left, the compass ring 19 from the right. The scissor is widened meanwhile and put back after.
namespace {
    bool hud_wide() {
        const auto& gfx = aidyn::settings::graphics();
        return gfx.hud_at_edges && gfx.aspect != aidyn::settings::Aspect::Original;
    }

    gpr hud_begin(uint8_t* rdram, gpr g, bool right) {
        GfxCommand c[5];
        gEXEnable(&c[0]);
        gEXSetScissor(&c[1], 0 /* G_SC_NON_INTERLACE */, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, 0, 0, 240);
        const int off = right ? -320 * 4 : 0;  // RIGHT adds the frame width to x
        gEXSetRectAlign(&c[3], right ? G_EX_ORIGIN_RIGHT : G_EX_ORIGIN_LEFT, right ? G_EX_ORIGIN_RIGHT : G_EX_ORIGIN_LEFT,
                        off, 0, off, 0);
        return put_cmds(rdram, g, c, 5);
    }

    gpr hud_end(uint8_t* rdram, gpr g) {
        const uint32_t h = MEM_HU(kHres1, (gpr)gGfxManager), v = MEM_HU(kVres1, (gpr)gGfxManager);
        GfxCommand c[3];
        gEXSetRectAlign(&c[0], G_EX_ORIGIN_NONE, G_EX_ORIGIN_NONE, 0, 0, 0, 0);
        c[2].values.word0 = 0xED000000;                                   // gDPSetScissor(G_SC_NON_INTERLACE, 0, 0, h, v)
        c[2].values.word1 = ((h * 4) & 0xFFF) << 12 | ((v * 4) & 0xFFF);
        return put_cmds(rdram, g, c, 3);
    }
}

extern "C" void aidyn_hud_begin(uint8_t* rdram, recomp_context* ctx, int right) {
    if (hud_wide()) {
        ctx->r4 = hud_begin(rdram, ctx->r4, right != 0);
    }
}

extern "C" void aidyn_hud_end(uint8_t* rdram, recomp_context* ctx) {
    if (hud_wide()) {
        ctx->r2 = hud_end(rdram, ctx->r2);
    }
}

// Fast-forward. Every mode of the game (walking, combat, menus, cutscenes) measures its frame's time step by counting
// the controller samples queued since the last frame: Controller::GetInput pops one and returns 1, and the frame calls
// it until it returns 0, then steps that many ticks of 1/60 s (capped at 6: made for the N64's 10 to 20 frames a
// second). aidyn.us.toml hooks GetInput's entry (the port) and its return: while fast-forward is on, every real sample
// is followed by (speed - 1) quiet ones, copies of it without its new presses (same stick, held buttons and hold
// times), so the game runs that many ticks a frame and no press is handled twice. Fast-forward is L3 or a held Tab
// (src/main/input.cpp), at game.fast_forward_speed; enemy turns in combat get game.combat_enemy_speed on their own:
// combat_controls (the combat frame's sample loop) is hooked on entry and looks at gCombatP->current_Ent->aiP (set for
// enemies and the AI-led Sholeh). While the player's fast-forward is on, a marker shows at the top right.
namespace {
    constexpr gpr kQuietSample = (gpr)(int32_t)0x80F20000;  // one ControllerFull (0x34 bytes), above the game's 15 MB
    constexpr int kControllerFull = 0x34;
    constexpr int kPressed = 0x8;                            // controller_aidyn.pressed
    constexpr gpr kCombatP = (gpr)(int32_t)0x800E0BF0;       // gCombatP
    constexpr int kCurrentEnt = 0x2C, kAiP = 0x44;           // CombatStruct.current_Ent, CombatEntity.aiP
    int ff_port = 0;
    int ff_pending = 0;   // quiet samples still to hand out this frame
    int ff_combat = 1;    // speed for the combat_controls call under way (an enemy's turn)
}

std::atomic_uint32_t aidyn_game_ticks{ 0 };  // samples handed out on port 1: game ticks (AIDYN_FPS prints the rate)

extern "C" void aidyn_ff_input_begin(int port) {
    ff_port = port;
}

// a0 (the caller's controller_aidyn**) was kept in s3; popped is GetInput's result.
extern "C" uint32_t aidyn_ff_input_end(uint8_t* rdram, gpr input_ptr, uint32_t popped) {
    if (ff_port != 0) {
        return popped;
    }
    const int speed = std::max(aidyn::input::fast_forward() ? aidyn::settings::game().fast_forward_speed : 1, ff_combat);
    if (popped) {
        if (speed > 1) {
            const gpr sample = (gpr)(int32_t)MEM_W(0, input_ptr);
            for (int i = 0; i < kControllerFull; i += 2) {
                MEM_H(i, kQuietSample) = MEM_H(i, sample);
            }
            MEM_W(kPressed, kQuietSample) = 0;
            ff_pending += speed - 1;
        }
        aidyn_game_ticks.fetch_add(1, std::memory_order_relaxed);
        return 1;
    }
    if (ff_pending > 0) {
        ff_pending--;
        MEM_W(0, input_ptr) = (int32_t)kQuietSample;
        aidyn_game_ticks.fetch_add(1, std::memory_order_relaxed);
        return 1;
    }
    ff_combat = 1;  // the queue is empty: the combat frame's sample loop (if that is where we are) is over
    return 0;
}

extern "C" void aidyn_ff_combat(uint8_t* rdram) {
    int speed = 1;
    const gpr combat = (gpr)(int32_t)MEM_W(0, kCombatP);
    if (combat != 0) {
        const gpr ent = (gpr)(int32_t)MEM_W(kCurrentEnt, combat);
        if (ent != 0 && MEM_W(kAiP, ent) != 0) {
            speed = aidyn::settings::game().combat_enemy_speed;
        }
    }
    ff_combat = speed;
}

// Graphics__EndList (the end of every frame's display list) draws this: the chevrons (branding image 4), 8 texels in
// from the top right of the picture, while the player's fast-forward is on.
gpr aidyn_ff_marker(uint8_t* rdram, gpr g) {
    if (!aidyn::input::fast_forward()) {
        return g;
    }
    copy_branding(rdram);
    const auto& img = aidyn::branding::kImages[4];
    const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
    auto cmd = [&](uint32_t w0, uint32_t w1) { MEM_W(0, g) = (int32_t)w0; MEM_W(4, g) = (int32_t)w1; g += 8; };
    g = hud_begin(rdram, g, true);
    cmd(0xE7000000, 0);                  // gDPPipeSync
    cmd(0xE3000A01, 0x00000000);         // gDPSetCycleType(G_CYC_1CYCLE)
    cmd(0xE3000C00, 0x00000000);         // gDPSetTexturePersp(G_TP_NONE)
    cmd(0xE3001001, 0x00000000);         // gDPSetTextureLUT(G_TT_NONE)
    cmd(0xE3001201, 0x00002000);         // gDPSetTextureFilter(G_TF_BILERP)
    cmd(0xE2001E01, 0x00000000);         // gDPSetAlphaCompare(G_AC_NONE)
    cmd(0xE200001C, 0x00504240);         // gDPSetRenderMode(G_RM_XLU_SURF, G_RM_XLU_SURF2)
    cmd(0xFCFFFFFF, 0xFFFCF279);         // gDPSetCombineMode(G_CC_DECALRGBA, G_CC_DECALRGBA)
    g = draw_rgba16(rdram, g, 4, (320 - 8 - img.w) * hs, 8 * vs, hs, vs);
    return hud_end(rdram, g);
}

// Combat HUD at the screen edges. The combat screen draws the enemies' column at the left (slots at x 21 to 47) and the
// party's at the right (x 270 to 298) in 320x240 coordinates, which RT64 keeps in the middle 4:3 of a widescreen
// picture: each slot a WidgetBorg8Combat frame, each fighter's portrait a WidgetBorg8Combat that flies in from the
// centre to its slot (crossbones are its child), the health bars drawn beside the slots by CombatDrawHealthbar, and
// the radar (WidgetCombatRadar, func_8008DF04) at the bottom right. With hud_at_edges, aidyn.us.toml hooks the entry
// and return of the two Render functions and the bar drawing, and each piece is drawn with RT64's rectangle alignment
// on its side: a slot frame by where it stands, a portrait by the slot it goes to (so its fly-in ends at the edge
// instead of jumping sides halfway), a bar by its x, the radar on the right. The message box and menus stay centred.
namespace {
    constexpr gpr kCombatPortraitCount = (gpr)(int32_t)0x800E3144;  // u8
    constexpr gpr kCombatPortraits = (gpr)(int32_t)0x800E3148;      // CombatPortrait* (0x18 each: widget, ..., blipIndex at 0x10)
    constexpr gpr kCombatRadarBlips = (gpr)(int32_t)0x800E314C;     // CombatRadarBlip* (0x18 each: widget, s16 x at 4)
    constexpr int kWidgetPosX = 0x60;                               // BaseWidget.posX
    int combat_depth = 0;
    bool combat_aligned[64];
    bool radar_aligned = false, bar_aligned = false;

    // Which edge a combat widget belongs to: false left, true right.
    bool combat_right(uint8_t* rdram, gpr widget) {
        const gpr portraits = (gpr)(int32_t)MEM_W(0, kCombatPortraits);
        const gpr blips = (gpr)(int32_t)MEM_W(0, kCombatRadarBlips);
        const int count = MEM_BU(0, kCombatPortraitCount);
        for (int i = 0; portraits != 0 && i < count; i++) {
            const gpr p = portraits + i * 0x18;
            if ((gpr)(int32_t)MEM_W(0, p) == widget) {
                const int blip = MEM_BU(0x10, p);
                if (blip != 0xFF && blips != 0) {
                    return (int16_t)MEM_H(4, blips + blip * 0x18) >= 160;
                }
                break;
            }
        }
        return (int16_t)MEM_H(kWidgetPosX, widget) >= 160;
    }
}

// WidgetBorg8Combat::Render(this, g, ...): this in a0, g in a1; returns g in v0. Only the outermost of nested renders
// (a portrait and its crossbones) sets the alignment.
extern "C" void aidyn_combat_widget_begin(uint8_t* rdram, recomp_context* ctx) {
    const int d = combat_depth++;
    if (d >= (int)std::size(combat_aligned)) {
        return;
    }
    combat_aligned[d] = d == 0 && hud_wide();
    if (combat_aligned[d]) {
        ctx->r5 = hud_begin(rdram, ctx->r5, combat_right(rdram, ctx->r4));
    }
}

extern "C" void aidyn_combat_widget_end(uint8_t* rdram, recomp_context* ctx) {
    const int d = --combat_depth;
    if (d >= 0 && d < (int)std::size(combat_aligned) && combat_aligned[d]) {
        ctx->r2 = hud_end(rdram, ctx->r2);
    }
    if (combat_depth < 0) {
        combat_depth = 0;
    }
}

extern "C" void aidyn_combat_radar_begin(uint8_t* rdram, recomp_context* ctx) {
    radar_aligned = hud_wide();
    if (radar_aligned) {
        ctx->r5 = hud_begin(rdram, ctx->r5, true);
    }
}

extern "C" void aidyn_combat_radar_end(uint8_t* rdram, recomp_context* ctx) {
    if (radar_aligned) {
        ctx->r2 = hud_end(rdram, ctx->r2);
        radar_aligned = false;
    }
}

// CombatDrawHealthbar: each bar is two DrawRectangle calls; g in s6 and the bar's x in s4 before them, g in v0 after.
extern "C" gpr aidyn_combat_bar_begin(uint8_t* rdram, gpr g, int x) {
    bar_aligned = hud_wide();
    return bar_aligned ? hud_begin(rdram, g, x >= 160) : g;
}

extern "C" gpr aidyn_combat_bar_end(uint8_t* rdram, gpr g) {
    if (bar_aligned) {
        bar_aligned = false;
        return hud_end(rdram, g);
    }
    return g;
}

// HD text widths. The game measures every letter's width from the original font sheet when it loads a face
// (Font::LoadFace, func_800AA474, through Font::SetupBorg8) and both draws that many columns of the letter's cell
// and advances by it. The original pixel font is wide and loosely spaced, so the HD pack's letters (tools/hdui/font.py)
// have their own proportions and these widths (src/game/font_widths.h, generated with them). aidyn.us.toml hooks
// LoadFace right after SetupBorg8 (s4 = the face's Borg8 index, s0 = its FontSubstruct, widths at +0x10) and the two
// font sheets (BORG8_MainFont 28, BORG8_SlimFont 29) get them, only when the HD UI pack is in use: with the original
// letters the original widths stay.
#include "font_widths.h"

namespace {
    bool hd_text() {
        static const bool on = [] {
            const auto& packs = aidyn::settings::graphics().texture_packs;
            if (std::find(packs.begin(), packs.end(), "hd-ui") == packs.end()) {
                return false;
            }
            std::error_code ec;
            const std::filesystem::path beside =
                std::filesystem::read_symlink("/proc/self/exe", ec).parent_path() / "texture-packs";
            for (const std::filesystem::path& dir : { aidyn::settings::folder() / "texture-packs", beside }) {
                if (std::filesystem::exists(dir / "hd-ui", ec) || std::filesystem::exists(dir / "hd-ui.rtz", ec)) {
                    return true;
                }
            }
            return false;
        }();
        return on;
    }
}

extern "C" void aidyn_font_widths(uint8_t* rdram, uint32_t borg, gpr face) {
    if ((borg != 28 && borg != 29) || !hd_text()) {
        return;
    }
    const gpr sizes = (gpr)(int32_t)MEM_W(0x10, face);
    for (const auto& w : aidyn::font::kWidths) {
        MEM_H(w.cell * 2, sizes) = w.width;
    }
}

// ---- Sound log (AIDYN_SFX_LOG) ----
// What the game asks to play: DCM::Add (every sound voice: footsteps, swings, hits, spells), PlayAudioSound (menu
// and dialogue sounds, by Borg12 index) and player_audiokey's per-frame check with the actor's proximity volume.
namespace {
bool sfx_log() {
    static const bool on = std::getenv("AIDYN_SFX_LOG") != nullptr;
    return on;
}
}

extern "C" void aidyn_sfx_key(uint8_t* rdram, recomp_context* ctx) {
    if (!sfx_log()) {
        return;
    }
    uint8_t prox = MEM_BU(0x68, ctx->r29);
    fprintf(stderr, "sfx key: actor %08X anim %u frame %u/%u prox %u\n", (uint32_t)ctx->r16, (uint32_t)ctx->r17,
            (uint32_t)ctx->r18, (uint32_t)ctx->r19, prox);
}

// Sound test (AIDYN_SOUND_TEST=<Borg12 index>, for exporting the game's music and sounds with AIDYN_AUDIO_DUMP): the
// title screen plays that Borg12 in place of its theme (load_intro_music hook), and nothing else gets a voice: the
// title's flying camera loads map areas whose own music and ambient sounds would otherwise mix in.
namespace {
    int sound_test_track() {
        static const int track = [] {
            const char* v = std::getenv("AIDYN_SOUND_TEST");
            return (v != nullptr && *v != '\0') ? (int)std::strtol(v, nullptr, 0) : -1;
        }();
        return track;
    }
}

extern "C" gpr aidyn_sound_test_track(gpr index) {
    const int track = sound_test_track();
    if (track < 0) {
        return index;
    }
    fprintf(stderr, "sound test: Borg12 %d (0x%X) in place of the title theme %u\n", track, track, (uint32_t)index);
    return (gpr)track;
}

// DCM::Add's entry; returns 1 to refuse the sound (the hook returns 0 from DCM::Add).
extern "C" int aidyn_sfx_dcm(uint8_t* rdram, recomp_context* ctx) {
    if (sfx_log()) {
        fprintf(stderr, "sfx dcm add: sub %08X vol %u pan %d\n", (uint32_t)ctx->r6, (uint32_t)(ctx->r7 & 0xFF),
                (int8_t)MEM_BU(0x10, ctx->r29));
    }
    if (sound_test_track() < 0) {
        return 0;
    }
    const gpr music = MEM_W(0, (gpr)(int32_t)0x800D809C);  // gGlobals.titleSplashVars.introMusic (Borg12Header*)
    const uint32_t mine = music != 0 ? (uint32_t)MEM_W(8, music) + 8 : 0;  // &header->dat->sub
    return (uint32_t)ctx->r6 != mine;
}

extern "C" void aidyn_sfx_play(uint8_t* rdram, recomp_context* ctx) {
    if (sfx_log()) {
        fprintf(stderr, "sfx play: borg12 %u\n", (uint32_t)ctx->r5);
    }
}

extern "C" void aidyn_sfx_attack_vol(uint8_t* rdram, recomp_context* ctx) {
    if (sfx_log()) {
        uint32_t bits = (uint32_t)MEM_W(0, (gpr)(int32_t)0x800DA0E0);  // gGlobals.VolSFX (gGlobals + 0x2058)
        float vol;
        memcpy(&vol, &bits, 4);
        fprintf(stderr, "sfx attack: actor %08X anim %u frame %u/%u vol8 %u VolSFX %.2f\n", (uint32_t)ctx->r18,
                (uint32_t)(ctx->r12 & 0xFFFF), (uint32_t)(ctx->r11 & 0xFFFF), (uint32_t)(ctx->r10 & 0xFFFF),
                (uint32_t)(ctx->r17 & 0xFF), vol);
    }
}

extern "C" void aidyn_sfx_attack_pick(uint8_t* rdram, recomp_context* ctx) {
    if (sfx_log()) {
        fprintf(stderr, "sfx attack match: actor %08X sound %u\n", (uint32_t)ctx->r18, (uint32_t)ctx->r2);
    }
}

// ---- Button prompts ----
// Controls card: while walking the field (gGlobals.gameStateA 1) or in combat (2), a see-through dark card at the left
// edge lists what the buttons do there (top left in the field; bottom left in combat, clear of the enemies' column).
// View (N64 C-left; keyboard J) shows and hides it in those two modes and isn't passed to the game there (it only
// cycled combat targets, which the stick and RT still do); src/main/input.cpp, saved as settings.json
// graphics.controls_card. Page prompts: the pause pages' arrows (SliderArrowL / LR / R, told apart by their pixels,
// at the bottom of the page, and the one painted into the map page's background) get LB and RB at the ends they
// point to. Both are drawn at the end of the frame (Graphics__EndList) with their own
// render state, from the pictures in src/game/branding_images.h (tools/branding/make_branding.py, which also puts
// their full-resolution versions in the rechronicled pack).
namespace {
    constexpr uint32_t kGameStateA = 0x800D80C8;  // gGlobals.gameStateA (u8)

    int branding_index(const char* name) {
        for (size_t i = 0; i < std::size(aidyn::branding::kImages); i++) {
            if (std::strcmp(aidyn::branding::kImages[i].name, name) == 0) {
                return (int)i;
            }
        }
        return -1;
    }

    // Texels from the left edge to the last column with any opaque texel.
    uint32_t ink_width(int index) {
        const auto& img = aidyn::branding::kImages[index];
        uint32_t w = 0;
        for (uint32_t y = 0; y < img.h; y++) {
            for (uint32_t x = 0; x < img.w; x++) {
                if (img.rgba16[y * img.w + x] & 1) {
                    w = std::max(w, x + 1);
                }
            }
        }
        return w;
    }

    // One-cycle, filtered, blended; texture colour, texture alpha times the primitive colour's alpha.
    gpr prompt_state(uint8_t* rdram, gpr g, uint8_t alpha) {
        auto cmd = [&](uint32_t w0, uint32_t w1) { MEM_W(0, g) = (int32_t)w0; MEM_W(4, g) = (int32_t)w1; g += 8; };
        cmd(0xE7000000, 0);                  // gDPPipeSync
        cmd(0xE3000A01, 0x00000000);         // gDPSetCycleType(G_CYC_1CYCLE)
        cmd(0xE3000C00, 0x00000000);         // gDPSetTexturePersp(G_TP_NONE)
        cmd(0xE3001001, 0x00000000);         // gDPSetTextureLUT(G_TT_NONE)
        cmd(0xE3001201, 0x00002000);         // gDPSetTextureFilter(G_TF_BILERP)
        cmd(0xE2001E01, 0x00000000);         // gDPSetAlphaCompare(G_AC_NONE)
        cmd(0xE200001C, 0x00504240);         // gDPSetRenderMode(G_RM_XLU_SURF, G_RM_XLU_SURF2)
        cmd(0xFC119623, 0xFF2FFFFF);         // gDPSetCombineMode(G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM)
        cmd(0xFA000000, 0xFFFFFF00u | alpha); // gDPSetPrimColor(0, 0, 255, 255, 255, alpha)
        return g;
    }

    struct PageArrow {
        bool left, right;
        float x, y, w, h, scale;  // screen pixels; pixels per texel
        uint8_t alpha;
    };
    std::vector<PageArrow> page_arrows;  // drawn this frame (game thread)

    struct Card {
        const char* rows[8];
        bool bottom;
    };
    const Card kFieldCard = { { "ctl_walk", "ctl_camera", "ctl_talk", "ctl_party", "ctl_sneak", "ctl_ff", "ctl_menu", nullptr },
                              false };
    const Card kCombatCard = { { "ctl_move", "ctl_attack", "ctl_magic", "ctl_endturn", "ctl_combatcam", "ctl_ff",
                                 "ctl_hide", nullptr }, true };

    gpr draw_card(uint8_t* rdram, gpr g, const Card& card) {
        constexpr float kMargin = 6.0f, kPad = 2.0f, kMessagePortraitX = 25.0f, kMessageTop = 178.0f;
        int rows[8], n = 0;
        uint32_t width = 0, height = 0;
        for (const char* name : card.rows) {
            if (name == nullptr) {
                break;
            }
            const int i = branding_index(name);
            if (i < 0) {
                continue;
            }
            rows[n++] = i;
            width = std::max(width, ink_width(i));
            height += aidyn::branding::kImages[i].h;
        }
        const int panel = branding_index("ctl_panel");
        if (n == 0 || panel < 0) {
            return g;
        }
        const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
        const float x0 = kMargin;
        float y0 = card.bottom ? 240.0f - kMargin - height - 2 * kPad : kMargin;
        // Combat's message box (centred, its portrait from x 25 to y 179 of the middle 4:3) leaves the bottom left
        // corner free on a wide picture only; on a narrower one the card goes above the box.
        const float room = kMessagePortraitX + (240.0f * view_aspect() - 320.0f) / 2.0f;
        if (card.bottom && x0 + width + 2 * kPad + 3.0f > room) {
            y0 = kMessageTop - 3.0f - height - 2 * kPad;
        }
        const auto& p = aidyn::branding::kImages[panel];
        g = hud_begin(rdram, g, false);
        g = prompt_state(rdram, g, 150);
        g = draw_rgba16(rdram, g, panel, x0 * hs, y0 * vs, (width + 2 * kPad) / p.w * hs, (height + 2 * kPad) / p.h * vs);
        g = prompt_state(rdram, g, 255);
        float y = y0 + kPad;
        for (int k = 0; k < n; k++) {
            g = draw_rgba16(rdram, g, rows[k], (x0 + kPad) * hs, y * vs, hs, vs);
            y += aidyn::branding::kImages[rows[k]].h;
        }
        return hud_end(rdram, g);
    }

    int play_mode(uint8_t* rdram) {
        const uint8_t state = MEM_BU(0, (gpr)(int32_t)kGameStateA);
        static const bool log = std::getenv("AIDYN_MODE_LOG") != nullptr;
        static uint8_t last = 0xFF;
        if (log && state != last) {
            fprintf(stderr, "game state %u\n", state);
            last = state;
        }
        return state == 1 ? 1 : state == 2 ? 2 : 0;
    }
}

// N64BorgImageDraw's entry: note a pause page arrow (a1 the image, a2/a3 x/y, sp+0x10.. xOff yOff h v xScale yScale r
// g b a).
// The map page's arrow is painted into its background (PauseBGMap, 320x240 CI8: a left arrow at texels 142..177,
// 196..203, told by those texels): noted at that place when the visible part of the picture includes it.
static void note_map_page_arrow(uint8_t* rdram, recomp_context* ctx, gpr img) {
    constexpr uint32_t kX = 142, kY = 196, kW = 36, kH = 8;
    const gpr pixels = MEM_W(0x14, img);
    uint64_t hash = 0xCBF29CE484222325ULL;
    for (uint32_t y = kY; y < kY + kH; y++) {
        for (uint32_t x = kX; x < kX + kW; x++) {
            hash = (hash ^ MEM_BU(y * 320 + x, pixels)) * 0x100000001B3ULL;
        }
    }
    const gpr sp = ctx->r29;
    const uint32_t xoff = MEM_HU(0x12, sp), yoff = MEM_HU(0x16, sp), h = MEM_HU(0x1A, sp), v = MEM_HU(0x1E, sp);
    if (hash != 0x0D50127AE284254EULL || xoff > kX || h < kX + kW || yoff > kY || v < kY + kH) {
        return;
    }
    const float hs = read_f32(rdram, (gpr)(int32_t)0x800E49A0), vs = read_f32(rdram, (gpr)(int32_t)0x800E49A4);
    float x, y;
    uint32_t bits = (uint32_t)ctx->r6;
    std::memcpy(&x, &bits, 4);
    bits = (uint32_t)ctx->r7;
    std::memcpy(&y, &bits, 4);
    const float xs = read_f32(rdram, sp + 0x20), ys = read_f32(rdram, sp + 0x24);
    PageArrow a{};
    a.left = true;
    a.x = (x + (float)(int)(kX - xoff) * xs) * hs;
    a.y = (y + (float)(int)(kY - yoff) * ys) * vs;
    a.w = kW * xs * hs;
    a.h = kH * ys * vs;
    a.scale = xs * hs;
    a.alpha = MEM_BU(0x37, sp);
    if (a.alpha != 0 && page_arrows.size() < 8) {
        page_arrows.push_back(a);
    }
}

void note_page_arrow(uint8_t* rdram, recomp_context* ctx) {
    const gpr img = ctx->r5;
    if (img != 0 && MEM_HU(0x08, img) == 4 && MEM_HU(0x0A, img) == 320 && MEM_HU(0x0C, img) == 240) {
        note_map_page_arrow(rdram, ctx, img);
        return;
    }
    if (img == 0 || MEM_HU(0x08, img) != 5 || MEM_HU(0x0C, img) != 8) {
        return;
    }
    const uint32_t w = MEM_HU(0x0A, img);
    if (w != 36 && w != 47) {
        return;
    }
    const gpr pixels = MEM_W(0x14, img);
    uint64_t hash = 0xCBF29CE484222325ULL;
    for (uint32_t i = 0; i < w * 8; i++) {
        hash = (hash ^ MEM_BU(i, pixels)) * 0x100000001B3ULL;
    }
    PageArrow a{};
    if (hash == 0xB250FD36B7D00719ULL) a.left = true;                          // SliderArrowL
    else if (hash == 0x46AC41CADC3C3186ULL) a.left = a.right = true;           // SliderArrowLR
    else if (hash == 0x30B0CB2D5BAD17CDULL) a.right = true;                    // SliderArrowR
    else return;
    const gpr sp = ctx->r29;
    const float hs = read_f32(rdram, (gpr)(int32_t)0x800E49A0), vs = read_f32(rdram, (gpr)(int32_t)0x800E49A4);
    float x, y;
    uint32_t bits = (uint32_t)ctx->r6;
    std::memcpy(&x, &bits, 4);
    bits = (uint32_t)ctx->r7;
    std::memcpy(&y, &bits, 4);
    const float xs = read_f32(rdram, sp + 0x20), ys = read_f32(rdram, sp + 0x24);
    a.x = x * hs;
    a.y = y * vs;
    a.w = (float)(int)(MEM_HU(0x1A, sp) - MEM_HU(0x12, sp)) * xs * hs;
    a.h = (float)(int)(MEM_HU(0x1E, sp) - MEM_HU(0x16, sp)) * ys * vs;
    a.scale = xs * hs;
    a.alpha = MEM_BU(0x37, sp);
    // Only a page's own arrow, at its bottom: the Controller Pak screens use the same arrows on their save slots
    // (y 77), which aren't turned with LB / RB.
    if (y < 180.0f) {
        return;
    }
    if (a.alpha != 0 && page_arrows.size() < 8) {
        page_arrows.push_back(a);
    }
}

namespace aidyn {
    bool redrawn_art();  // src/main/rt64_context.cpp: false while F2 has the redrawn art pack off
}

gpr aidyn_prompts(uint8_t* rdram, gpr g) {
    if (!aidyn::redrawn_art()) {  // say so, top right under the fast-forward marker
        copy_branding(rdram);
        const int label = branding_index("art_off");
        if (label >= 0) {
            const auto& img = aidyn::branding::kImages[label];
            const float hs = MEM_HU(kHres1, (gpr)gGfxManager) / 320.0f, vs = MEM_HU(kVres1, (gpr)gGfxManager) / 240.0f;
            g = hud_begin(rdram, g, true);
            g = prompt_state(rdram, g, 255);
            g = draw_rgba16(rdram, g, label, (320 - 8 - img.w) * hs, 22 * vs, hs, vs);
            g = hud_end(rdram, g);
        }
    }
    const int mode = play_mode(rdram);
    aidyn::input::set_card_mode(mode);
    if (mode == 0 && page_arrows.empty()) {
        return g;
    }
    copy_branding(rdram);
    // Not while Menu is held: pausing keeps a picture of the screen as the pause menu's background.
    if (mode != 0 && aidyn::input::controls_card() && !aidyn::input::start_held()) {
        g = draw_card(rdram, g, mode == 1 ? kFieldCard : kCombatCard);
    }
    const int lb = branding_index("prompt_lb"), rb = branding_index("prompt_rb");
    for (const PageArrow& a : page_arrows) {
        if (lb < 0 || rb < 0) {
            break;
        }
        const auto& icon = aidyn::branding::kImages[lb];
        const float iw = icon.w * a.scale, ih = icon.h * a.scale, y = a.y + (a.h - ih) / 2.0f, gap = a.scale;
        g = prompt_state(rdram, g, a.alpha);
        if (a.left) {
            g = draw_rgba16(rdram, g, lb, a.x - iw - gap, y, a.scale, a.scale);
        }
        if (a.right) {
            g = draw_rgba16(rdram, g, rb, a.x + a.w + gap, y, a.scale, a.scale);
        }
    }
    page_arrows.clear();
    return g;
}
