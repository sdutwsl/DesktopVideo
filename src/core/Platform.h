#pragma once

#include <memory>
#include <string>
#include <vector>

namespace muralop {

class MpvPlayer;

struct MonitorInfo {
    std::string id;     // stable name: "\\.\DISPLAY1" on Windows, connector name ("DP-1") on Wayland
    int x = 0, y = 0;   // logical position in the virtual desktop
    int width = 0, height = 0;
    bool primary = false;   // the one that gets audio
};

// A render target covering exactly one monitor, placed below the desktop icons.
// Created by the platform layer; the core only ever talks to it through this interface.
class Surface {
public:
    virtual ~Surface() = default;
    virtual const MonitorInfo& monitor() const = 0;
    // Route the player's video output into this surface and initialise the player.
    virtual void attach(MpvPlayer& player) = 0;
    // Undo attach(); called before the player is destroyed (the surface still exists).
    virtual void detach() = 0;
    // Restore a visible frame after the desktop/session becomes available again.
    // Native window surfaces are repainted by their platform unless overridden.
    virtual void requestRedraw() {}
};

// Everything the core needs from the OS.
class Platform {
public:
    virtual ~Platform() = default;
    virtual std::vector<MonitorInfo> monitors() = 0;
    virtual std::unique_ptr<Surface> createSurface(const MonitorInfo& monitor) = 0;
};

} // namespace muralop
