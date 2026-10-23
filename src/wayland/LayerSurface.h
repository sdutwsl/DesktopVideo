#pragma once

#include "EventLoop.h"
#include "WaylandDisplay.h"
#include "core/Platform.h"

#include <EGL/egl.h>
#include <wayland-client.h>
#include <wayland-egl.h>

struct zwlr_layer_surface_v1;
struct wp_viewport;
struct wp_fractional_scale_v1;
struct mpv_render_context;

namespace vw {

// A background layer-shell surface covering one output. mpv decodes on its own
// threads and draws through its render API into our EGL surface on the loop thread.
class LayerSurface : public Surface, public EventLoop::Source {
public:
    LayerSurface(WaylandDisplay& display, Output& output, const MonitorInfo& monitor);
    ~LayerSurface() override;

    // Surface
    const MonitorInfo& monitor() const override { return monitor_; }
    void attach(MpvPlayer& player) override;
    void detach() override;

    // EventLoop::Source, woken by mpv (new frame or pending events)
    int fd() override { return wakeFd_; }
    void dispatch(short revents) override;

private:
    void configure(uint32_t serial, uint32_t width, uint32_t height);
    void setPreferredScale(uint32_t scale120);
    void resizeBuffer();
    void render();
    void frameDone();
    void wake();
    bool makeCurrent();

    WaylandDisplay& display_;
    Output& output_;
    MonitorInfo monitor_;

    wl_surface* surface_ = nullptr;
    zwlr_layer_surface_v1* layer_ = nullptr;
    wp_viewport* viewport_ = nullptr;
    wp_fractional_scale_v1* fractional_ = nullptr;
    wl_callback* frameCallback_ = nullptr;
    wl_egl_window* eglWindow_ = nullptr;
    EGLSurface eglSurface_ = EGL_NO_SURFACE;

    int logicalWidth_ = 0, logicalHeight_ = 0;
    int bufferWidth_ = 0, bufferHeight_ = 0;
    uint32_t preferredScale120_ = 0;   // from wp_fractional_scale_v1, 0 = unknown
    bool configured_ = false;
    bool closed_ = false;
    bool dirty_ = false;   // a frame arrived while a frame callback was pending
    unsigned framesDrawn_ = 0;   // VW_DEBUG statistics

    MpvPlayer* player_ = nullptr;
    mpv_render_context* render_ = nullptr;
    int wakeFd_ = -1;
};

} // namespace vw
