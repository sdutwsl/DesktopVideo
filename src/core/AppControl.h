#pragma once

#include "Config.h"
#include "Wallpaper.h"
#include <functional>
#include <string>

namespace muralop {

// Shared command interface: newline-separated arguments and JSON replies.
// The Node plugin calls this on its render thread; standalone shells also
// expose it over a session bus or a per-user Windows named pipe.
class AppControl {
public:
    AppControl(Wallpaper& wallpaper, Config& config, std::function<void()> quit);
    std::string execute(const std::string& request);
    std::string state() const;

private:
    Wallpaper& wallpaper_;
    Config& config_;
    std::function<void()> quit_;
};

} // namespace muralop
