#pragma once

#include "EventLoop.h"
#include "GlRenderer.h"
#include "WaylandDisplay.h"
#include "core/FrameQueue.h"
#include "core/Platform.h"

#include <EGL/egl.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include <atomic>

struct zwlr_layer_surface_v1;
struct wp_viewport;
struct wp_fractional_scale_v1;

namespace vw {

// A background layer-shell surface covering one output. VLC decodes into the
// frame queue from its own threads; frames are drawn here on the loop thread.
class LayerSurface : public Surface, public FrameSink, public EventLoop::Source {
public:
    LayerSurface(WaylandDisplay& display, Output& output, const MonitorInfo& monitor);
    ~LayerSurface() override;

    // Surface
    const MonitorInfo& monitor() const override { return monitor_; }
    void attach(VlcPlayer& player) override;

    // FrameSink, called from VLC threads
    void setup(const VideoFormat& format) override;
    void* lock(void** planes) override;
    void unlock(void* handle) override;
    void display(void* handle) override;
    void cleanup() override;

    // EventLoop::Source, woken by display()
    int fd() override { return wakeFd_; }
    void dispatch(short revents) override;

private:
    void configure(uint32_t serial, uint32_t width, uint32_t height);
    void setPreferredScale(uint32_t scale120);
    void resizeBuffer();
    void render();
    void frameDone();

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

    FrameQueue queue_;
    GlRenderer::Textures textures_;
    VideoFormat format_;
    std::atomic<bool> formatChanged_{false};
    std::atomic<bool> formatCleared_{false};
    int wakeFd_ = -1;
};

} // namespace vw
