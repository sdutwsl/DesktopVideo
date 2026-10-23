#pragma once

#include "EventLoop.h"
#include "GlRenderer.h"
#include "core/Platform.h"

#include <EGL/egl.h>
#include <wayland-client.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

struct zwlr_layer_shell_v1;
struct zxdg_output_manager_v1;
struct zxdg_output_v1;
struct wp_viewporter;
struct wp_fractional_scale_manager_v1;
struct zwlr_foreign_toplevel_manager_v1;
struct zwlr_foreign_toplevel_handle_v1;

namespace vw {

struct Output {
    uint32_t globalName = 0;
    wl_output* output = nullptr;
    zxdg_output_v1* xdgOutput = nullptr;
    std::string name;          // connector name, e.g. "DP-1"
    int32_t x = 0, y = 0;      // logical geometry
    int32_t width = 0, height = 0;
    int32_t scale = 1;
    // raw wl_output data, used when xdg-output is unavailable
    int32_t geomX = 0, geomY = 0, modeWidth = 0, modeHeight = 0;
    bool done = false;
};

// Connection to the compositor plus the globals the wallpaper needs. Also the
// vw::Platform implementation: monitors are outputs, surfaces are layer surfaces.
class WaylandDisplay : public Platform, public EventLoop::Source {
public:
    explicit WaylandDisplay(EventLoop& loop);
    ~WaylandDisplay() override;

    // Connect, bind globals, wait for the initial output list. False with a reason on failure.
    bool connect(std::string& error);

    // Platform
    std::vector<MonitorInfo> monitors() override;
    std::unique_ptr<Surface> createSurface(const MonitorInfo& monitor) override;

    // EventLoop::Source
    int fd() override;
    void prepare() override;
    void dispatch(short revents) override;

    std::function<void()> onOutputsChanged;
    std::function<void(const std::string& outputName, bool covered)> onOutputCovered;
    std::function<void()> onDisconnected;

    // For LayerSurface
    wl_display* display() const { return display_; }
    wl_compositor* compositor() const { return compositor_; }
    zwlr_layer_shell_v1* layerShell() const { return layerShell_; }
    wp_viewporter* viewporter() const { return viewporter_; }
    wp_fractional_scale_manager_v1* fractionalScale() const { return fractionalScale_; }
    EGLDisplay eglDisplay() const { return eglDisplay_; }
    EGLConfig eglConfig() const { return eglConfig_; }
    EGLContext eglContext() const { return eglContext_; }
    EventLoop& loop() { return loop_; }
    GlRenderer& renderer();   // requires a current EGL context

private:
    struct Toplevel {
        WaylandDisplay* display = nullptr;
        zwlr_foreign_toplevel_handle_v1* handle = nullptr;
        std::set<wl_output*> outputs, pendingOutputs;
        bool maximized = false, fullscreen = false, minimized = false;
        bool pMaximized = false, pFullscreen = false, pMinimized = false;
    };

    static void registryGlobal(void*, wl_registry*, uint32_t, const char*, uint32_t);
    static void registryGlobalRemove(void*, wl_registry*, uint32_t);
    void addOutput(uint32_t name, uint32_t version);
    void removeOutput(uint32_t name);
    void bindXdgOutput(Output& out);
    void outputDone(Output& out);
    bool initEgl(std::string& error);
    void updateCoverage();

    EventLoop& loop_;
    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_compositor* compositor_ = nullptr;
    zwlr_layer_shell_v1* layerShell_ = nullptr;
    zxdg_output_manager_v1* xdgOutputManager_ = nullptr;
    wp_viewporter* viewporter_ = nullptr;
    wp_fractional_scale_manager_v1* fractionalScale_ = nullptr;
    zwlr_foreign_toplevel_manager_v1* toplevelManager_ = nullptr;

    std::vector<std::unique_ptr<Output>> outputs_;
    std::vector<MonitorInfo> lastMonitors_;
    std::map<zwlr_foreign_toplevel_handle_v1*, std::unique_ptr<Toplevel>> toplevels_;
    std::map<wl_output*, bool> covered_;

    EGLDisplay eglDisplay_ = EGL_NO_DISPLAY;
    EGLConfig eglConfig_ = nullptr;
    EGLContext eglContext_ = EGL_NO_CONTEXT;
    GlRenderer renderer_;
    bool rendererReady_ = false;
    bool reading_ = false;
    bool connected_ = false;
};

} // namespace vw
