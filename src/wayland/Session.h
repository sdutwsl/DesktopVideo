#pragma once

#include <sdbus-c++/sdbus-c++.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace muralop {

// Lock screen, suspend and shutdown notifications from the desktop's D-Bus services.
class SessionMonitor {
public:
    // systemBus may be null when the system bus is unreachable (no logind).
    SessionMonitor(sdbus::IConnection& sessionBus, sdbus::IConnection* systemBus);

    std::function<void(bool locked)> onLocked;
    std::function<void(bool sleeping)> onSleep;
    std::function<void()> onShutdown;

private:
    std::vector<sdbus::Slot> matches_;
};

// Native "open file" dialog through the xdg-desktop-portal, the only toolkit-free
// way to get one on Wayland.
class FileChooser {
public:
    explicit FileChooser(sdbus::IConnection& sessionBus);

    // Calls back with the chosen path, or an empty string if the dialog was cancelled or unavailable.
    void open(const std::string& title, std::function<void(std::string path)> onDone);

private:
    sdbus::IConnection& bus_;
    std::unique_ptr<sdbus::IProxy> portal_;
    sdbus::Slot response_;
    unsigned counter_ = 0;
};

// Our own control interface so a second invocation (or busctl) can talk to the daemon.
class Control {
public:
    static const char* SERVICE;
    static const char* PATH;
    static const char* INTERFACE;

    struct Handlers {
        std::function<void(std::string path, std::string monitor)> setVideo;
        std::function<void(std::string monitor)> chooseVideo;
        std::function<void(bool)> setPaused;
        std::function<void(bool)> setMute;
        std::function<void(int)> setVolume;
        std::function<void()> quit;
        std::function<std::vector<std::string>()> monitors;
    };

    Control(sdbus::IConnection& bus, Handlers handlers);

private:
    std::unique_ptr<sdbus::IObject> object_;
    Handlers handlers_;
};

} // namespace muralop
