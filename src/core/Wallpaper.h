#pragma once

#include "Config.h"
#include "Platform.h"
#include "MpvPlayer.h"

#include <chrono>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace muralop {

// Platform-independent heart of the program: one player per monitor, which video
// each one shows, and whether playback should currently be running.
class Wallpaper {
public:
    Wallpaper(Platform& platform, Config& config);
    ~Wallpaper();

    // Sync surfaces and players with the platform's monitor list (startup and hotplug).
    // Monitors that did not change keep playing untouched.
    void refreshMonitors();
    // Tear every surface down and start over (the desktop host window was recreated).
    void rebuildSurfaces();
    void updateOnlineOptions();

    // Video selection. An empty monitorId sets the default used by every monitor
    // without its own override. Paths are persisted in the config.
    void setVideo(const std::string& path, const std::string& monitorId = "");
    void setResolvedVideo(const ResolvedMedia& media, const std::string& monitorId);
    // Stops the players showing that selection (empty: the default video).
    void stopPlayback(const std::string& monitorId);
    std::string playbackState(const std::string& monitorId = "") const;
    void clearOverride(const std::string& monitorId);
    std::string videoFor(const std::string& monitorId) const;
    bool hasOverride(const std::string& monitorId) const;

    // Pause reasons. Playback runs only while none of them applies.
    void setUserPaused(bool paused);
    bool userPaused() const { return userPaused_; }
    void setLocked(bool locked);
    void setSuspended(bool suspended);

    void setVolume(int volume);
    int volume() const;
    void setMute(bool mute);
    bool mute() const;

    std::vector<MonitorInfo> monitors() const;
    std::string lastError() const;

    // Stop all players; called before the platform tears down its display connection.
    void shutdown();

private:
    struct Slot {
        MonitorInfo monitor;
        std::unique_ptr<Surface> surface;   // declared before player: destroyed after it
        std::unique_ptr<MpvPlayer> player;
        bool audio = false;
        ~Slot()
        {
            if (surface && player)
                surface->detach();
        }
    };

    std::string audioMonitorId(const std::vector<MonitorInfo>& monitors) const;
    void openSlot(Slot& slot);
    void applyPlayback();
    Slot* find(const std::string& monitorId);

    Platform& platform_;
    Config& config_;
    std::vector<std::unique_ptr<Slot>> slots_;
    struct Resolved {
        ResolvedMedia media;
        std::chrono::steady_clock::time_point at;
    };
    std::map<std::string, Resolved> resolved_;
    bool userPaused_ = false;
    bool locked_ = false;
    bool suspended_ = false;
};

} // namespace muralop
