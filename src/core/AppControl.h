#pragma once

#include "Config.h"
#include "Wallpaper.h"
#include <functional>
#include <deque>
#include <string>
#include <utility>

namespace muralop {

// Shared command interface: newline-separated arguments and JSON replies.
// The Node plugin calls this on its render thread; standalone shells also
// expose it over a session bus or a per-user Windows named pipe.
class AppControl {
public:
    AppControl(Wallpaper& wallpaper, Config& config, std::function<void()> quit);
    std::string execute(const std::string& request);
    std::string state() const;
    // Called only on the native event-loop thread. The plugin host drains these
    // events to route window/playlist actions through the application.
    void postEvent(std::string action, std::string value = {});
    std::function<void(const std::string&, bool, bool, const std::string&)> onMediaContext;

private:
    Wallpaper& wallpaper_;
    Config& config_;
    std::function<void()> quit_;
    std::deque<std::pair<std::string, std::string>> events_;
};

} // namespace muralop
