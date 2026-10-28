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

struct ResolvedMedia {
    std::string source, video, audio, userAgent, referrer;
    std::string title = {};
};

struct MediaInfo {
    std::string source, title;
    double position = 0, duration = 0;
    bool paused = false, seekable = false;
    uint64_t seekSerial = 0;
    std::string playback = "idle";
    std::string monitor = {};
    uint64_t revision = 0;
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
    void setOnlineOptions(const OnlineOptions& online);
    mpv_handle* handle() const { return mpv_; }

    void processEvents();

    // Loops the file forever. Returns false if mpv rejected the command.
    bool open(const std::string& path, bool withAudio);
    bool openResolved(const ResolvedMedia& media, bool withAudio);
    void stop();
    void setPaused(bool paused);
    void seek(double seconds, bool absolute);
    MediaInfo mediaInfo() const;

    void setVolume(int volume);   // 0..100
    void setMute(bool mute);

    const std::string& path() const { return path_; }
    const std::string& lastError() const { return lastError_; }
    const std::string& playbackState() const { return playbackState_; }

private:
    static constexpr int MAX_RETRIES = 2;
    static void wakeupCb(void* self);
    bool loadPath(const std::string& path, bool withAudio);
    bool loadResolved(const ResolvedMedia& media, bool withAudio);

    mpv_handle* mpv_ = nullptr;
    bool initialized_ = false;
    uint64_t loadRequest_ = 0;
    int64_t loadEntry_ = -1, currentEntry_ = -1;
    int retries_ = 0;
    std::function<void()> retry_;
    std::string path_;
    std::string lastError_;
    std::string playbackState_ = "idle";
    std::string title_, mediaTitle_;
    double position_ = 0, duration_ = 0;
    bool paused_ = false, seekable_ = false, seeking_ = false;
    uint64_t seekSerial_ = 0;
    std::mutex wakeupMutex_;
    std::function<void()> wakeup_;
};

} // namespace muralop
