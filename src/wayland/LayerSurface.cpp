#include "LayerSurface.h"
#include "core/MpvPlayer.h"

#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include <GLES2/gl2.h>
#include <mpv/render_gl.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace muralop {

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

    // KWin maps the "desktop" namespace to WindowType::Desktop. Its overview
    // background item selects desktop windows, independently of the shell layer.
    layer_ = zwlr_layer_shell_v1_get_layer_surface(display_.layerShell(), surface_, output_.output,
                                                   ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "desktop");
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
    detach();
    display_.loop().remove(this);
    if (eglSurface_ != EGL_NO_SURFACE) {
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

static void* getProcAddress(void*, const char* name)
{
    return reinterpret_cast<void*>(eglGetProcAddress(name));
}

bool LayerSurface::makeCurrent()
{
    // Before the first configure there is no EGL surface yet; mpv's render context is
    // created then with no surface bound (EGL_KHR_surfaceless_context, which Mesa has).
    return eglMakeCurrent(display_.eglDisplay(), eglSurface_, eglSurface_, display_.eglContext());
}

void LayerSurface::attach(MpvPlayer& player)
{
    player_ = &player;
    player.setOption("vo", "libmpv");
    player.setWakeup([this] { wake(); });
    if (!player.initialize())
        return;

    // The render context has to exist before a file is loaded, or mpv disables video.
    if (!makeCurrent()) {
        fprintf(stderr, "[%s] cannot make the EGL context current (0x%x)\n", monitor_.id.c_str(), eglGetError());
        return;
    }
    mpv_opengl_init_params gl{};
    gl.get_proc_address = getProcAddress;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl},
        {MPV_RENDER_PARAM_WL_DISPLAY, display_.display()},   // lets VAAPI hand frames to GL directly
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    const int err = mpv_render_context_create(&render_, player.handle(), params);
    if (err < 0) {
        fprintf(stderr, "[%s] mpv render context: %s\n", monitor_.id.c_str(), mpv_error_string(err));
        render_ = nullptr;
        return;
    }
    mpv_render_context_set_update_callback(render_, [](void* self) { static_cast<LayerSurface*>(self)->wake(); }, this);
}

void LayerSurface::detach()
{
    if (player_)
        player_->setWakeup(nullptr);
    if (render_) {
        makeCurrent();
        mpv_render_context_free(render_);   // must happen before the player's core goes away
        render_ = nullptr;
    }
    player_ = nullptr;
}

void LayerSurface::wake()
{
    const uint64_t one = 1;
    [[maybe_unused]] ssize_t n = write(wakeFd_, &one, sizeof one);
}

void LayerSurface::requestRedraw()
{
    // Session callbacks can run before WaylandDisplay has completed/cancelled
    // its pending display read. Defer EGL rendering to our own dispatch source.
    redrawRequested_ = true;
    wake();
}

void LayerSurface::restoreFrame()
{
    if (!configured_ || closed_ || eglSurface_ == EGL_NO_SURFACE)
        return;
    // A frame callback is only a pacing hint. It may remain pending while the
    // lock screen obscures us; do not let it gate the first frame after unlock.
    if (frameCallback_) {
        wl_callback_destroy(frameCallback_);
        frameCallback_ = nullptr;
    }
    dirty_ = false;
    if (!makeCurrent()) {
        fprintf(stderr, "[%s] cannot restore EGL drawing after session resume (0x%x)\n",
                monitor_.id.c_str(), eglGetError());
        return;
    }
    if (render_)
        mpv_render_context_update(render_);
    render();
}

// ---- layer surface / scale events (loop thread) -------------------------------

void LayerSurface::configure(uint32_t serial, uint32_t width, uint32_t height)
{
    if (getenv("MURALOP_DEBUG"))
        fprintf(stderr, "[%s] layer configure %ux%u\n", monitor_.id.c_str(), width, height);
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
    if (!makeCurrent())
        return;
    eglSwapInterval(dpy, 0);   // we pace ourselves with frame callbacks

    if (render_) {
        mpv_opengl_fbo fbo{};
        fbo.w = bufferWidth_;
        fbo.h = bufferHeight_;
        int flipY = 1;
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
            {MPV_RENDER_PARAM_FLIP_Y, &flipY},
            {MPV_RENDER_PARAM_INVALID, nullptr},
        };
        mpv_render_context_render(render_, params);
        if (getenv("MURALOP_DEBUG") && (++framesDrawn_ == 1 || framesDrawn_ % 600 == 0))
            fprintf(stderr, "[%s] frames drawn: %u (%dx%d)\n", monitor_.id.c_str(), framesDrawn_, bufferWidth_,
                    bufferHeight_);
    } else {
        glViewport(0, 0, bufferWidth_, bufferHeight_);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    frameCallback_ = wl_surface_frame(surface_);
    static const wl_callback_listener listener = {
        [](void* data, wl_callback*, uint32_t) { static_cast<LayerSurface*>(data)->frameDone(); },
    };
    wl_callback_add_listener(frameCallback_, &listener, this);
    eglSwapBuffers(dpy, eglSurface_);   // commits the surface
    if (render_)
        mpv_render_context_report_swap(render_);
}

void LayerSurface::dispatch(short revents)
{
    if (!(revents & 1 /*POLLIN*/))
        return;
    uint64_t v;
    [[maybe_unused]] ssize_t n = read(wakeFd_, &v, sizeof v);
    if (player_)
        player_->processEvents();
    if (redrawRequested_) {
        redrawRequested_ = false;
        restoreFrame();
    } else if (render_ && (mpv_render_context_update(render_) & MPV_RENDER_UPDATE_FRAME))
        render();
}

} // namespace muralop
