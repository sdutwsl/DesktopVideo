#pragma once

#include "core/Platform.h"

#include <memory>
#include <vector>

namespace muralop {

// macOS implementation of the platform layer: monitors come from NSScreen, and each
// surface is a borderless window at the desktop window level (above the system
// wallpaper, below the Finder's desktop icons) that mpv draws into through its
// render API and a CAOpenGLLayer.
class MacPlatform : public Platform {
public:
    std::vector<MonitorInfo> monitors() override;
    std::unique_ptr<Surface> createSurface(const MonitorInfo& monitor) override;
};

} // namespace muralop
