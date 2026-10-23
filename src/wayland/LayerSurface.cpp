#include "LayerSurface.h"
#include "core/VlcPlayer.h"

#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <sys/eventfd.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>

namespace vw {

LayerSurface::LayerSurface(WaylandDisplay& display, Output& output, const MonitorInfo& monitor)
    : display_(display), output_(output), monitor_(monitor)
{
    wakeFd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    display_.loop().add(this);

    surface_ = wl_compositor_create_surface(display_.compositor());

    // Nothing on the wallpaper should ever take input.
    wl_region* empty = wl_compositor_create_region(display_.compositor());
    wl_surface_set_input_region(surface_, empty);
    wl_region_destroy(empty);

    if (display_.viewporter())
        viewport_ = wp_viewporter_get_viewport(display_.viewporter(), surface_);

    if (display_.fractionalScale()) {
        fractional_ = wp_fractional_scale_manager_v1_get_fractional_scale(display_.fractionalScale(), surface_);
        static const wp_fractional_scale_v1_listener listener = {
            [](void* data, wp_fractional_scale_v1*, uint32_t scale) {
                static_cast<LayerSurface*>(data)->setPreferredScale(scale);
            },
        };
        wp_fractional_scale_v1_add_listener(fractional_, &listener, this);
    }

    layer_ = zwlr_layer_shell_v1_get_layer_surface(display_.layerShell(), surface_, output_.output,
                                                   ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "wallpaper");
    zwlr_layer_surface_v1_set_anchor(layer_, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                                 ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(layer_, -1);   // ignore panels, cover the whole output
    zwlr_layer_surface_v1_set_keyboard_interactivity(layer_, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    zwlr_layer_surface_v1_set_size(layer_, 0, 0);           // compositor picks the output size

    static const zwlr_layer_surface_v1_listener listener = {
        [](void* data, zwlr_layer_surface_v1*, uint32_t serial, uint32_t w, uint32_t h) {
            static_cast<LayerSurface*>(data)->configure(serial, w, h);
        },
        [](void* data, zwlr_layer_surface_v1*) { static_cast<LayerSurface*>(data)->closed_ = true; },
    };
    zwlr_layer_surface_v1_add_listener(layer_, &listener, this);
    wl_surface_commit(surface_);
}

LayerSurface::~LayerSurface()
{
    display_.loop().remove(this);
    if (eglSurface_ != EGL_NO_SURFACE) {
        eglMakeCurrent(display_.eglDisplay(), eglSurface_, eglSurface_, display_.eglContext());
        display_.renderer().release(textures_);
        eglMakeCurrent(display_.eglDisplay(), EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(display_.eglDisplay(), eglSurface_);
    }
    if (eglWindow_)
        wl_egl_window_destroy(eglWindow_);
    if (frameCallback_)
        wl_callback_destroy(frameCallback_);
    if (fractional_)
        wp_fractional_scale_v1_destroy(fractional_);
    if (viewport_)
        wp_viewport_destroy(viewport_);
    if (layer_)
        zwlr_layer_surface_v1_destroy(layer_);
    if (surface_)
        wl_surface_destroy(surface_);
    if (wakeFd_ >= 0)
        close(wakeFd_);
}

void LayerSurface::attach(VlcPlayer& player)
{
    player.setFrameSink(this);
}

// ---- layer surface / scale events (loop thread) -------------------------------

void LayerSurface::configure(uint32_t serial, uint32_t width, uint32_t height)
{
    zwlr_layer_surface_v1_ack_configure(layer_, serial);
    logicalWidth_ = width ? int(width) : monitor_.width;
    logicalHeight_ = height ? int(height) : monitor_.height;
    configured_ = true;
    resizeBuffer();
    render();
}

void LayerSurface::setPreferredScale(uint32_t scale120)
{
    if (preferredScale120_ == scale120)
        return;
    preferredScale120_ = scale120;
    if (configured_) {
        resizeBuffer();
        render();
    }
}

void LayerSurface::resizeBuffer()
{
    double scale = output_.scale;
    if (preferredScale120_ > 0 && viewport_)
        scale = preferredScale120_ / 120.0;

    const int w = std::max(1, int(std::lround(logicalWidth_ * scale)));
    const int h = std::max(1, int(std::lround(logicalHeight_ * scale)));

    if (viewport_) {
        wp_viewport_set_destination(viewport_, logicalWidth_, logicalHeight_);
    } else {
        wl_surface_set_buffer_scale(surface_, int(scale));
    }

    // The video covers everything; tell the compositor so it can skip blending.
    wl_region* opaque = wl_compositor_create_region(display_.compositor());
    wl_region_add(opaque, 0, 0, logicalWidth_, logicalHeight_);
    wl_surface_set_opaque_region(surface_, opaque);
    wl_region_destroy(opaque);

    if (w == bufferWidth_ && h == bufferHeight_)
        return;
    bufferWidth_ = w;
    bufferHeight_ = h;

    if (!eglWindow_) {
        eglWindow_ = wl_egl_window_create(surface_, w, h);
        eglSurface_ = eglCreatePlatformWindowSurface(display_.eglDisplay(), display_.eglConfig(), eglWindow_, nullptr);
        if (eglSurface_ == EGL_NO_SURFACE)
            fprintf(stderr, "[%s] eglCreatePlatformWindowSurface failed: 0x%x\n", monitor_.id.c_str(), eglGetError());
    } else {
        wl_egl_window_resize(eglWindow_, w, h, 0, 0);
    }
}

void LayerSurface::frameDone()
{
    wl_callback_destroy(frameCallback_);
    frameCallback_ = nullptr;
    if (dirty_) {
        dirty_ = false;
        render();
    }
}

void LayerSurface::render()
{
    if (!configured_ || closed_ || eglSurface_ == EGL_NO_SURFACE)
        return;
    if (frameCallback_) {
        dirty_ = true;   // compositor has not consumed the previous buffer yet
        return;
    }

    EGLDisplay dpy = display_.eglDisplay();
    if (!eglMakeCurrent(dpy, eglSurface_, eglSurface_, display_.eglContext()))
        return;
    eglSwapInterval(dpy, 0);   // we pace ourselves with frame callbacks
    GlRenderer& gl = display_.renderer();

    if (formatCleared_.exchange(false))
        gl.release(textures_);
    if (formatChanged_.exchange(false)) {
        format_ = queue_.format();
        gl.allocate(textures_, format_);
    }
    if (Frame* frame = queue_.acquireLatest()) {
        if (textures_.id[0])
            gl.upload(textures_, format_, *frame);
        queue_.release(frame);
    }
    gl.draw(textures_.id[0] ? &textures_ : nullptr, format_, bufferWidth_, bufferHeight_);

    frameCallback_ = wl_surface_frame(surface_);
    static const wl_callback_listener listener = {
        [](void* data, wl_callback*, uint32_t) { static_cast<LayerSurface*>(data)->frameDone(); },
    };
    wl_callback_add_listener(frameCallback_, &listener, this);
    eglSwapBuffers(dpy, eglSurface_);   // commits the surface
}

void LayerSurface::dispatch(short revents)
{
    if (!(revents & 1 /*POLLIN*/))
        return;
    uint64_t v;
    [[maybe_unused]] ssize_t n = read(wakeFd_, &v, sizeof v);
    render();
}

// ---- FrameSink (VLC threads) ---------------------------------------------------

void LayerSurface::setup(const VideoFormat& format)
{
    queue_.configure(format);
    formatChanged_ = true;
}

void* LayerSurface::lock(void** planes)
{
    const VideoFormat f = queue_.format();
    Frame* frame = queue_.acquireForWrite();
    for (int p = 0; p < 3; ++p)
        planes[p] = frame->plane(f, p);
    return frame;
}

void LayerSurface::unlock(void* handle)
{
    queue_.commit(static_cast<Frame*>(handle));
}

void LayerSurface::display(void*)
{
    const uint64_t one = 1;
    [[maybe_unused]] ssize_t n = write(wakeFd_, &one, sizeof one);
}

void LayerSurface::cleanup()
{
    queue_.reset();
    formatCleared_ = true;
    const uint64_t one = 1;
    [[maybe_unused]] ssize_t n = write(wakeFd_, &one, sizeof one);
}

} // namespace vw
