#pragma once

#include "Config.h"
#include "Wallpaper.h"
#include <functional>
#include <string>

namespace muralop {

// Local GUI protocol: command and arguments separated by newlines; JSON replies.
// It is carried over the session bus or a per-user Windows named pipe, never TCP.
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
