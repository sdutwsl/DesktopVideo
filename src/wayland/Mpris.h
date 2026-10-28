#pragma once

#include "EventLoop.h"
#include "core/Wallpaper.h"

#include <sdbus-c++/sdbus-c++.h>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace muralop {

// One MPRIS player for the application, on the existing session connection.
// Metadata follows the audio monitor; playback controls apply to all monitors.
class Mpris : public EventLoop::Source {
public:
    struct Actions {
        std::function<void()> raise, quit, next, previous;
        std::function<void(std::string)> openUri;
        std::function<void(std::string)> changed;
        std::function<void(std::string)> control;
    };

    Mpris(EventLoop& loop, sdbus::IConnection& bus, Wallpaper& wallpaper, Actions actions);
    ~Mpris() override;
    void setNavigation(const std::string& source, bool next, bool previous, const std::string& monitor);

    int fd() override { return -1; }
    int timeoutMs() override;
    void dispatch(short) override;

private:
    struct Snapshot {
        std::string source, title, status;
        int64_t duration = 0, position = 0;
        uint64_t seekSerial = 0, revision = 0;
        double volume = 0;
        bool canPlay = false, canPause = false, canSeek = false, next = false, previous = false;
    };
    Snapshot snapshot() const;
    std::map<std::string, sdbus::Variant> metadata() const;
    sdbus::ObjectPath trackId() const;
    void changed(const std::string& action);
    void control(const std::string& action);
    void refresh();

    EventLoop& loop_;
    sdbus::IConnection& bus_;
    Wallpaper& wallpaper_;
    Actions actions_;
    std::unique_ptr<sdbus::IObject> object_;
    std::string navigationSource_, navigationMonitor_;
    bool next_ = false, previous_ = false;
    std::optional<Snapshot> previousSnapshot_;
    std::chrono::steady_clock::time_point updateAt_{};
};

} // namespace muralop
