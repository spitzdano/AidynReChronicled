// RT64 renderer glue for N64ModernRuntime. Adapted from RecompFrontend's rt64_render_context
// (N64Recomp/RecompFrontend @ b2c5c18, MIT/GPL-compatible) with the UI, mod and texture-pack
// parts removed: this shell has no menus, so there is nothing to draw over the game.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "ultramodern/renderer_context.hpp"

namespace RT64 {
    struct Application;
}

namespace aidyn {
    class RT64Context final : public ultramodern::renderer::RendererContext {
    public:
        RT64Context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, ultramodern::renderer::PresentationMode presentation_mode, bool developer_mode);
        ~RT64Context() override;

        bool valid() override { return static_cast<bool>(app); }
        bool update_config(const ultramodern::renderer::GraphicsConfig& old_config, const ultramodern::renderer::GraphicsConfig& new_config) override;
        void enable_instant_present() override;
        void send_dl(const OSTask* task) override;
        void send_dummy_workload(uint32_t fb_address) override;
        void update_screen() override;
        void shutdown() override;
        uint32_t get_display_framerate() const override;
        float get_resolution_scale() const override;

    private:
        void update_menu_aspect();
        int frames_without_menu_ = 0;
        std::unique_ptr<RT64::Application> app;
    };

    // F2: the redrawn art pack (rechronicled-art) off and on again, to compare with what is under it.
    void toggle_redrawn_art();
    // The pause menu's ReChronicled page changed settings.json graphics.textures / vsync: apply on the next frame.
    void refresh_textures();
    void refresh_vsync();
    float shown_fps();       // the frames shown a second (the FPS counter), measured twice a second
    void refresh_aspect();   // the Display page's Widescreen: picture shape again on the next frame
    bool redrawn_art();
    // The redrawn art pack (rechronicled-art) is listed in settings.json and was found (folder or .rtz).
    bool redrawn_art_available();

    // Save pictures (src/game/save_pictures.cpp). RT64 copies part of a framebuffer's full-resolution picture (area in
    // native pixels; see SharedQueueResources::FramebufferCapture) before it draws the next frame, and calls done with
    // RGBA8 rows on its own thread (empty when the framebuffer has no picture).
    void capture_framebuffer(uint32_t address, float left, float top, float right, float bottom,
                             std::function<void(std::vector<uint8_t>&& rgba, uint32_t width, uint32_t height)> done);
    // ... and asks, for each new texture, whether a save picture replaces it (defined in save_pictures.cpp).
    bool save_picture_for_texture(uint32_t address, uint16_t width, uint16_t height, uint8_t fmt, uint8_t siz,
                                  std::vector<uint8_t>& rgba, uint32_t& out_width, uint32_t& out_height);
    // The gap filler's horizon (src/game/patches.cpp "Below the horizon"): the row of the 240-line picture where
    // this frame's sky meets the ground, or -1 (no sky: indoors). Below it the ground is everywhere, so any background
    // there is a hole, and wider ones are filled.
    void set_gap_fill_horizon(int row);
    // ... or a strip of a page whose painted words are redrawn (defined in page_words.cpp).
    bool page_words_for_texture(uint64_t hash, uint32_t address, uint16_t width, uint16_t height, uint8_t fmt, uint8_t siz,
                                std::vector<uint8_t>& rgba, uint32_t& out_width, uint32_t& out_height);

    std::unique_ptr<ultramodern::renderer::RendererContext> create_render_context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode);
}
