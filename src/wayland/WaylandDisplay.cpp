#include "WaylandDisplay.h"
#include "LayerSurface.h"

#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

#include <EGL/eglext.h>
#include <poll.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace vw {

// ---- wl_output / xdg_output listeners ----------------------------------------

static void outputGeometry(void* data, wl_output*, int32_t x, int32_t y, int32_t, int32_t, int32_t,
                           const char*, const char*, int32_t)
{
    auto* out = static_cast<Output*>(data);
    out->geomX = x;
    out->geomY = y;
}

static void outputMode(void* data, wl_output*, uint32_t flags, int32_t width, int32_t height, int32_t)
{
    auto* out = static_cast<Output*>(data);
    if (flags & WL_OUTPUT_MODE_CURRENT) {
        out->modeWidth = width;
        out->modeHeight = height;
    }
}

static void outputScale(void* data, wl_output*, int32_t factor)
{
    static_cast<Output*>(data)->scale = factor;
}

static void outputName(void* data, wl_output*, const char* name)
{
    static_cast<Output*>(data)->name = name;
}

static void outputDescription(void*, wl_output*, const char*) {}

static void xdgLogicalPosition(void* data, zxdg_output_v1*, int32_t x, int32_t y)
{
    auto* out = static_cast<Output*>(data);
    out->x = x;
    out->y = y;
}

static void xdgLogicalSize(void* data, zxdg_output_v1*, int32_t width, int32_t height)
{
    auto* out = static_cast<Output*>(data);
    out->width = width;
    out->height = height;
}

static void xdgName(void* data, zxdg_output_v1*, const char* name)
{
    auto* out = static_cast<Output*>(data);
    if (out->name.empty())
        out->name = name;
}

static void xdgDescription(void*, zxdg_output_v1*, const char*) {}

// ---- WaylandDisplay -----------------------------------------------------------

WaylandDisplay::WaylandDisplay(EventLoop& loop) : loop_(loop) {}

WaylandDisplay::~WaylandDisplay()
{
    if (connected_)
        loop_.remove(this);
    renderer_.destroy();
    if (eglDisplay_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (eglContext_ != EGL_NO_CONTEXT)
            eglDestroyContext(eglDisplay_, eglContext_);
        eglTerminate(eglDisplay_);
    }
    for (auto& [handle, t] : toplevels_)
        zwlr_foreign_toplevel_handle_v1_destroy(handle);
    toplevels_.clear();
    if (toplevelManager_)
        zwlr_foreign_toplevel_manager_v1_destroy(toplevelManager_);
    for (auto& out : outputs_) {
        if (out->xdgOutput)
            zxdg_output_v1_destroy(out->xdgOutput);
        wl_output_destroy(out->output);
    }
    outputs_.clear();
    if (fractionalScale_)
        wp_fractional_scale_manager_v1_destroy(fractionalScale_);
    if (viewporter_)
        wp_viewporter_destroy(viewporter_);
    if (xdgOutputManager_)
        zxdg_output_manager_v1_destroy(xdgOutputManager_);
    if (layerShell_)
        zwlr_layer_shell_v1_destroy(layerShell_);
    if (compositor_)
        wl_compositor_destroy(compositor_);
    if (registry_)
        wl_registry_destroy(registry_);
    if (display_) {
        wl_display_flush(display_);
        wl_display_disconnect(display_);
    }
}

void WaylandDisplay::registryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface,
                                    uint32_t version)
{
    auto* self = static_cast<WaylandDisplay*>(data);
    auto bind = [&](const wl_interface* iface, uint32_t maxVersion) {
        return wl_registry_bind(registry, name, iface, std::min(version, maxVersion));
    };

    if (!std::strcmp(interface, wl_compositor_interface.name)) {
        self->compositor_ = static_cast<wl_compositor*>(bind(&wl_compositor_interface, 4));
    } else if (!std::strcmp(interface, zwlr_layer_shell_v1_interface.name)) {
        self->layerShell_ = static_cast<zwlr_layer_shell_v1*>(bind(&zwlr_layer_shell_v1_interface, 4));
    } else if (!std::strcmp(interface, zxdg_output_manager_v1_interface.name)) {
        self->xdgOutputManager_ = static_cast<zxdg_output_manager_v1*>(bind(&zxdg_output_manager_v1_interface, 3));
        for (auto& out : self->outputs_)
            self->bindXdgOutput(*out);
    } else if (!std::strcmp(interface, wp_viewporter_interface.name)) {
        self->viewporter_ = static_cast<wp_viewporter*>(bind(&wp_viewporter_interface, 1));
    } else if (!std::strcmp(interface, wp_fractional_scale_manager_v1_interface.name)) {
        self->fractionalScale_ =
            static_cast<wp_fractional_scale_manager_v1*>(bind(&wp_fractional_scale_manager_v1_interface, 1));
    } else if (!std::strcmp(interface, wl_output_interface.name)) {
        self->addOutput(name, std::min(version, 4u));
    } else if (!std::strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name)) {
        self->toplevelManager_ =
            static_cast<zwlr_foreign_toplevel_manager_v1*>(bind(&zwlr_foreign_toplevel_manager_v1_interface, 3));

        static const zwlr_foreign_toplevel_manager_v1_listener managerListener = {
            // toplevel
            [](void* data, zwlr_foreign_toplevel_manager_v1*, zwlr_foreign_toplevel_handle_v1* handle) {
                auto* self = static_cast<WaylandDisplay*>(data);
                auto t = std::make_unique<Toplevel>();
                t->display = self;
                t->handle = handle;

                static const zwlr_foreign_toplevel_handle_v1_listener handleListener = {
                    [](void*, zwlr_foreign_toplevel_handle_v1*, const char*) {},   // title
                    [](void*, zwlr_foreign_toplevel_handle_v1*, const char*) {},   // app_id
                    // output_enter
                    [](void* data, zwlr_foreign_toplevel_handle_v1*, wl_output* output) {
                        static_cast<Toplevel*>(data)->pendingOutputs.insert(output);
                    },
                    // output_leave
                    [](void* data, zwlr_foreign_toplevel_handle_v1*, wl_output* output) {
                        static_cast<Toplevel*>(data)->pendingOutputs.erase(output);
                    },
                    // state
                    [](void* data, zwlr_foreign_toplevel_handle_v1*, wl_array* states) {
                        auto* t = static_cast<Toplevel*>(data);
                        t->pMaximized = t->pFullscreen = t->pMinimized = false;
                        const auto* begin = static_cast<const uint32_t*>(states->data);
                        const auto* end = begin + states->size / sizeof(uint32_t);
                        for (const uint32_t* s = begin; s != end; ++s) {
                            if (*s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED) t->pMaximized = true;
                            if (*s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN) t->pFullscreen = true;
                            if (*s == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED) t->pMinimized = true;
                        }
                    },
                    // done
                    [](void* data, zwlr_foreign_toplevel_handle_v1*) {
                        auto* t = static_cast<Toplevel*>(data);
                        t->outputs = t->pendingOutputs;
                        t->maximized = t->pMaximized;
                        t->fullscreen = t->pFullscreen;
                        t->minimized = t->pMinimized;
                        t->display->updateCoverage();
                    },
                    // closed
                    [](void* data, zwlr_foreign_toplevel_handle_v1* handle) {
                        WaylandDisplay* self = static_cast<Toplevel*>(data)->display;
                        zwlr_foreign_toplevel_handle_v1_destroy(handle);
                        self->toplevels_.erase(handle);
                        self->updateCoverage();
                    },
                    [](void*, zwlr_foreign_toplevel_handle_v1*, zwlr_foreign_toplevel_handle_v1*) {},   // parent
                };
                zwlr_foreign_toplevel_handle_v1_add_listener(handle, &handleListener, t.get());
                self->toplevels_[handle] = std::move(t);
            },
            // finished
            [](void*, zwlr_foreign_toplevel_manager_v1*) {},
        };
        zwlr_foreign_toplevel_manager_v1_add_listener(self->toplevelManager_, &managerListener, self);
    }
}

void WaylandDisplay::registryGlobalRemove(void* data, wl_registry*, uint32_t name)
{
    static_cast<WaylandDisplay*>(data)->removeOutput(name);
}

void WaylandDisplay::addOutput(uint32_t name, uint32_t version)
{
    auto out = std::make_unique<Output>();
    out->globalName = name;
    out->output = static_cast<wl_output*>(wl_registry_bind(registry_, name, &wl_output_interface, version));

    static const wl_output_listener listener = {
        outputGeometry,
        outputMode,
        [](void* data, wl_output*) { /* done */
            auto* out = static_cast<Output*>(data);
            out->done = true;
        },
        outputScale,
        outputName,
        outputDescription,
    };
    wl_output_add_listener(out->output, &listener, out.get());
    bindXdgOutput(*out);
    outputs_.push_back(std::move(out));
}

void WaylandDisplay::bindXdgOutput(Output& out)
{
    if (!xdgOutputManager_ || out.xdgOutput)
        return;
    out.xdgOutput = zxdg_output_manager_v1_get_xdg_output(xdgOutputManager_, out.output);
    static const zxdg_output_v1_listener listener = {
        xdgLogicalPosition,
        xdgLogicalSize,
        [](void* data, zxdg_output_v1*) { static_cast<Output*>(data)->done = true; },   // done (v1/v2 only)
        xdgName,
        xdgDescription,
    };
    zxdg_output_v1_add_listener(out.xdgOutput, &listener, &out);
}

void WaylandDisplay::removeOutput(uint32_t name)
{
    auto it = std::find_if(outputs_.begin(), outputs_.end(),
                           [&](const std::unique_ptr<Output>& o) { return o->globalName == name; });
    if (it == outputs_.end())
        return;
    covered_.erase((*it)->output);
    if ((*it)->xdgOutput)
        zxdg_output_v1_destroy((*it)->xdgOutput);
    wl_output_destroy((*it)->output);
    outputs_.erase(it);
}

bool WaylandDisplay::connect(std::string& error)
{
    display_ = wl_display_connect(nullptr);
    if (!display_) {
        error = "cannot connect to the Wayland display (is WAYLAND_DISPLAY set?)";
        return false;
    }
    registry_ = wl_display_get_registry(display_);
    static const wl_registry_listener registryListener = {registryGlobal, registryGlobalRemove};
    wl_registry_add_listener(registry_, &registryListener, this);

    wl_display_roundtrip(display_);   // globals
    wl_display_roundtrip(display_);   // output details, xdg_output, initial toplevels

    if (!compositor_) {
        error = "compositor lacks wl_compositor";
        return false;
    }
    if (!layerShell_) {
        error = "compositor does not support zwlr_layer_shell_v1 (GNOME does not; KDE, Sway, Hyprland, niri do)";
        return false;
    }
    if (!initEgl(error))
        return false;

    fprintf(stderr, "[wayland] layer-shell ok, viewporter %s, fractional-scale %s, foreign-toplevel %s\n",
            viewporter_ ? "yes" : "no", fractionalScale_ ? "yes" : "no",
            toplevelManager_ ? "yes" : "no (cannot pause behind fullscreen windows)");

    lastMonitors_ = monitors();
    connected_ = true;
    loop_.add(this);
    return true;
}

bool WaylandDisplay::initEgl(std::string& error)
{
    eglDisplay_ = eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, display_, nullptr);
    if (eglDisplay_ == EGL_NO_DISPLAY || !eglInitialize(eglDisplay_, nullptr, nullptr)) {
        error = "EGL initialisation failed";
        return false;
    }
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 0,
        EGL_NONE,
    };
    EGLint count = 0;
    if (!eglChooseConfig(eglDisplay_, attribs, &eglConfig_, 1, &count) || count == 0) {
        error = "no suitable EGL config";
        return false;
    }
    const EGLint ctxAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    eglContext_ = eglCreateContext(eglDisplay_, eglConfig_, EGL_NO_CONTEXT, ctxAttribs);
    if (eglContext_ == EGL_NO_CONTEXT) {
        error = "cannot create an OpenGL ES 2 context";
        return false;
    }
    return true;
}

GlRenderer& WaylandDisplay::renderer()
{
    if (!rendererReady_) {
        std::string err;
        rendererReady_ = renderer_.init(err);
        if (!rendererReady_)
            fprintf(stderr, "[gl] shader setup failed: %s\n", err.c_str());
    }
    return renderer_;
}

std::vector<MonitorInfo> WaylandDisplay::monitors()
{
    std::vector<MonitorInfo> result;
    for (const auto& out : outputs_) {
        if (!out->done || out->name.empty())
            continue;
        MonitorInfo m;
        m.id = out->name;
        if (out->xdgOutput && out->width > 0) {
            m.x = out->x;
            m.y = out->y;
            m.width = out->width;
            m.height = out->height;
        } else {
            m.x = out->geomX;
            m.y = out->geomY;
            m.width = out->modeWidth / std::max(out->scale, 1);
            m.height = out->modeHeight / std::max(out->scale, 1);
        }
        if (m.width <= 0 || m.height <= 0)
            continue;
        result.push_back(m);
    }
    // Wayland has no notion of a primary output; treat the one at the origin as primary.
    auto origin = std::find_if(result.begin(), result.end(), [](const MonitorInfo& m) { return m.x == 0 && m.y == 0; });
    if (origin != result.end())
        origin->primary = true;
    else if (!result.empty())
        result.front().primary = true;
    return result;
}

std::unique_ptr<Surface> WaylandDisplay::createSurface(const MonitorInfo& monitor)
{
    for (auto& out : outputs_)
        if (out->name == monitor.id)
            return std::make_unique<LayerSurface>(*this, *out, monitor);
    return nullptr;
}

void WaylandDisplay::updateCoverage()
{
    std::map<wl_output*, bool> now;
    for (const auto& out : outputs_)
        now[out->output] = false;
    for (const auto& [handle, t] : toplevels_) {
        if (t->minimized || !(t->maximized || t->fullscreen))
            continue;
        for (wl_output* o : t->outputs)
            now[o] = true;
    }
    for (const auto& [output, covered] : now) {
        auto prev = covered_.find(output);
        if (prev != covered_.end() && prev->second == covered)
            continue;
        covered_[output] = covered;
        for (const auto& out : outputs_) {
            if (out->output != output || out->name.empty())
                continue;
            if (getenv("VW_DEBUG"))
                fprintf(stderr, "[%s] %s\n", out->name.c_str(), covered ? "covered by a maximized/fullscreen window" : "uncovered");
            if (onOutputCovered)
                onOutputCovered(out->name, covered);
        }
    }
}

// ---- EventLoop::Source ---------------------------------------------------------

int WaylandDisplay::fd()
{
    return display_ ? wl_display_get_fd(display_) : -1;
}

void WaylandDisplay::prepare()
{
    while (wl_display_prepare_read(display_) != 0)
        wl_display_dispatch_pending(display_);
    wl_display_flush(display_);
    reading_ = true;
}

void WaylandDisplay::dispatch(short revents)
{
    if (reading_) {
        if (revents & POLLIN)
            wl_display_read_events(display_);
        else
            wl_display_cancel_read(display_);
        reading_ = false;
    }
    if ((revents & (POLLHUP | POLLERR)) || wl_display_dispatch_pending(display_) < 0) {
        if (onDisconnected)
            onDisconnected();
        return;
    }

    // Report output changes once every output has settled.
    std::vector<MonitorInfo> now = monitors();
    bool same = now.size() == lastMonitors_.size();
    for (size_t i = 0; same && i < now.size(); ++i)
        same = now[i].id == lastMonitors_[i].id && now[i].x == lastMonitors_[i].x && now[i].y == lastMonitors_[i].y &&
               now[i].width == lastMonitors_[i].width && now[i].height == lastMonitors_[i].height;
    if (!same) {
        lastMonitors_ = now;
        if (onOutputsChanged)
            onOutputsChanged();
    }
}

} // namespace vw
