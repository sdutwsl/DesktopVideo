#include "WaylandDisplay.h"
#include "LayerSurface.h"

#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

#include <EGL/eglext.h>
#include <poll.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace muralop {

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
    if (eglDisplay_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (eglContext_ != EGL_NO_CONTEXT)
            eglDestroyContext(eglDisplay_, eglContext_);
        eglTerminate(eglDisplay_);
    }
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
    wl_display_roundtrip(display_);   // output details, xdg_output

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

    fprintf(stderr, "[wayland] layer-shell ok, viewporter %s, fractional-scale %s\n",
            viewporter_ ? "yes" : "no", fractionalScale_ ? "yes" : "no");

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
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,   // ES3-capable configs carry this bit too
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 0,
        EGL_NONE,
    };
    EGLint count = 0;
    if (!eglChooseConfig(eglDisplay_, attribs, &eglConfig_, 1, &count) || count == 0) {
        error = "no suitable EGL config";
        return false;
    }
    // mpv renders best on ES 3 (high bit depth textures, VAAPI interop); ES 2 still works.
    for (EGLint version : {3, 2}) {
        const EGLint ctxAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, version, EGL_NONE};
        eglContext_ = eglCreateContext(eglDisplay_, eglConfig_, EGL_NO_CONTEXT, ctxAttribs);
        if (eglContext_ != EGL_NO_CONTEXT)
            return true;
    }
    error = "cannot create an OpenGL ES context";
    return false;
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

} // namespace muralop
