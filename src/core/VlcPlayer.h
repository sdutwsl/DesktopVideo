#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct libvlc_instance_t;
struct libvlc_media_player_t;

namespace vw {

class FrameSink;

// One libvlc instance shared by every player in the process.
class VlcInstance {
public:
    explicit VlcInstance(const std::vector<std::string>& args);
    ~VlcInstance();
    VlcInstance(const VlcInstance&) = delete;
    VlcInstance& operator=(const VlcInstance&) = delete;

    libvlc_instance_t* get() const { return instance_; }
    bool ok() const { return instance_ != nullptr; }

private:
    libvlc_instance_t* instance_ = nullptr;
};

// Thin wrapper around a libvlc media player. Video can go either to a native
// window (Windows HWND) or to a FrameSink that we render ourselves (Wayland).
class VlcPlayer {
public:
    explicit VlcPlayer(VlcInstance& instance);
    ~VlcPlayer();
    VlcPlayer(const VlcPlayer&) = delete;
    VlcPlayer& operator=(const VlcPlayer&) = delete;

    // Output routing; call one of these before open().
    void setNativeWindow(void* hwnd);
    void setFrameSink(FrameSink* sink);

    // Crop the picture to this aspect so an embedded window is filled edge to edge.
    // Only meaningful in native-window mode; the frame-sink path crops when drawing.
    void setFillAspect(int width, int height);

    // Opens the file looping forever and starts playback. Returns false if libvlc refused it.
    bool open(const std::string& path, bool withAudio);
    void stop();
    void setPaused(bool paused);
    bool playing() const;

    void setVolume(int volume);   // 0..100
    void setMute(bool mute);

    const std::string& path() const { return path_; }

private:
    static unsigned formatCb(void** opaque, char* chroma, unsigned* width, unsigned* height,
                             unsigned* pitches, unsigned* lines);
    static void cleanupCb(void* opaque);
    static void* lockCb(void* opaque, void** planes);
    static void unlockCb(void* opaque, void* picture, void* const* planes);
    static void displayCb(void* opaque, void* picture);

    VlcInstance& instance_;
    libvlc_media_player_t* player_ = nullptr;
    FrameSink* sink_ = nullptr;
    std::string path_;
    std::string cropAspect_;
};

} // namespace vw
