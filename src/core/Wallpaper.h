#pragma once

#include "Config.h"
#include "Platform.h"
#include "VlcPlayer.h"

#include <memory>
#include <string>
#include <vector>

namespace vw {

// Platform-independent heart of the program: one player per monitor, which video
// each one shows, and whether playback should currently be running.
class Wallpaper {
public:
    Wallpaper(Platform& platform, VlcInstance& vlc, Config& config);
    ~Wallpaper();

    // Sync surfaces and players with the platform's monitor list (startup and hotplug).
    // Monitors that did not change keep playing untouched.
    void refreshMonitors();
    // Tear every surface down and start over (the desktop host window was recreated).
    void rebuildSurfaces();

    // Video selection. An empty monitorId sets the default used by every monitor
    // without its own override. Paths are persisted in the config.
    void setVideo(const std::string& path, const std::string& monitorId = "");
    void clearOverride(const std::string& monitorId);
    std::string videoFor(const std::string& monitorId) const;
    bool hasOverride(const std::string& monitorId) const;

    // Pause reasons. Playback runs only while none of them applies.
    void setUserPaused(bool paused);
    bool userPaused() const { return userPaused_; }
    void setLocked(bool locked);
    void setSuspended(bool suspended);
    void setCovered(const std::string& monitorId, bool covered);   // fullscreen/maximized window on top

    void setVolume(int volume);
    int volume() const;
    void setMute(bool mute);
    bool mute() const;
    void setPauseWhenCovered(bool enabled);
    bool pauseWhenCovered() const;

    std::vector<MonitorInfo> monitors() const;

    // Stop all players; called before the platform tears down its display connection.
    void shutdown();

private:
    struct Slot {
        MonitorInfo monitor;
        std::unique_ptr<Surface> surface;   // declared before player: destroyed after it
        std::unique_ptr<VlcPlayer> player;
        bool audio = false;
        bool covered = false;
    };

    std::string audioMonitorId(const std::vector<MonitorInfo>& monitors) const;
    void openSlot(Slot& slot);
    void applyPlayback();
    Slot* find(const std::string& monitorId);

    Platform& platform_;
    VlcInstance& vlc_;
    Config& config_;
    std::vector<std::unique_ptr<Slot>> slots_;
    bool userPaused_ = false;
    bool locked_ = false;
    bool suspended_ = false;
};

} // namespace vw
