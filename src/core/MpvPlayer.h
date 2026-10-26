#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

struct mpv_handle;

namespace muralop {

struct OnlineOptions {
    std::string extractorPath;
    std::string cookieFile;
    std::string format;
};

// Thin wrapper around one libmpv core. Video goes either into a native window
// (Windows HWND, mpv renders itself) or through mpv's render API into a GL
// surface we own (Wayland); the surface picks one in Surface::attach().
class MpvPlayer {
public:
    explicit MpvPlayer(const OnlineOptions& online = {});
    ~MpvPlayer();
    MpvPlayer(const MpvPlayer&) = delete;
    MpvPlayer& operator=(const MpvPlayer&) = delete;

    // ---- Set up by the surface, before initialize() ----
    void setOption(const char* name, const char* value);
    void setWindow(int64_t wid);   // native window to draw into
    // Called from mpv's threads whenever events are pending; the surface must then
    // call processEvents() on its own thread. Pass nullptr to stop the calls.
    void setWakeup(std::function<void()> wakeup);
    bool initialize();
    mpv_handle* handle() const { return mpv_; }

    void processEvents();

    // Loops the file forever. Returns false if mpv rejected the command.
    bool open(const std::string& path, bool withAudio);
    void stop();
    void setPaused(bool paused);

    void setVolume(int volume);   // 0..100
    void setMute(bool mute);

    const std::string& path() const { return path_; }
    const std::string& lastError() const { return lastError_; }

private:
    static void wakeupCb(void* self);

    mpv_handle* mpv_ = nullptr;
    std::string path_;
    std::string lastError_;
    std::mutex wakeupMutex_;
    std::function<void()> wakeup_;
};

} // namespace muralop
