#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace vw {

// Planar I420 layout. Pitches are 64-byte aligned so the renderer can treat
// the pitch as texture width and rescale texture coordinates by width/pitch.
struct VideoFormat {
    unsigned width = 0, height = 0;     // visible size
    unsigned pitch[3] = {0, 0, 0};      // bytes per row, per plane
    unsigned lines[3] = {0, 0, 0};      // rows, per plane

    static VideoFormat i420(unsigned width, unsigned height);
    size_t planeSize(int p) const { return size_t(pitch[p]) * lines[p]; }
};

struct Frame {
    std::vector<uint8_t> data;   // three planes back to back
    uint8_t* plane(const VideoFormat& f, int p);
};

// Receives decoded frames from VLC's threads. Implemented by the platform's
// surface when the video is drawn by us rather than by a native VLC window.
class FrameSink {
public:
    virtual ~FrameSink() = default;
    virtual void setup(const VideoFormat& format) = 0;   // allocate buffers
    virtual void* lock(void** planes) = 0;               // hand VLC a buffer to write into
    virtual void unlock(void* handle) = 0;               // VLC is done writing
    virtual void display(void* handle) = 0;              // frame is ready to show
    virtual void cleanup() = 0;
};

// Triple buffer between VLC's decoder thread (producer) and the render thread (consumer).
// The producer never blocks; the consumer always gets the newest complete frame.
class FrameQueue {
public:
    void configure(const VideoFormat& format);
    void reset();
    VideoFormat format() const;

    // Producer side (any thread)
    Frame* acquireForWrite();
    void commit(Frame* frame);

    // Consumer side (render thread). acquireLatest() returns nullptr when nothing new arrived
    // since the last call; release() hands the buffer back once it has been uploaded.
    Frame* acquireLatest();
    void release(Frame* frame);

private:
    enum class State { Free, Writing, Ready, Reading };
    struct Slot { Frame frame; State state = State::Free; };

    mutable std::mutex mutex_;
    VideoFormat format_;
    Slot slots_[3];
    int ready_ = -1;   // index of the newest committed frame, or -1
};

} // namespace vw
