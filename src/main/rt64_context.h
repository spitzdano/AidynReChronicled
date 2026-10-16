// RT64 renderer glue for N64ModernRuntime. Adapted from RecompFrontend's rt64_render_context
// (N64Recomp/RecompFrontend @ b2c5c18, MIT/GPL-compatible) with the UI, mod and texture-pack
// parts removed: this shell has no menus, so there is nothing to draw over the game.
#pragma once

#include <memory>

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
    bool redrawn_art();

    std::unique_ptr<ultramodern::renderer::RendererContext> create_render_context(uint8_t* rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode);
}
