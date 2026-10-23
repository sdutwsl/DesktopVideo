#include "FrameQueue.h"

namespace vw {

static unsigned alignUp(unsigned v, unsigned a) { return (v + a - 1) / a * a; }

VideoFormat VideoFormat::i420(unsigned width, unsigned height)
{
    VideoFormat f;
    f.width = width;
    f.height = height;
    // Luma pitch aligned to 64 so the chroma pitch (half of it) stays 32-aligned;
    // the same width/pitch ratio then applies to all three planes.
    f.pitch[0] = alignUp(width, 64);
    f.pitch[1] = f.pitch[2] = f.pitch[0] / 2;
    f.lines[0] = alignUp(height, 2);
    f.lines[1] = f.lines[2] = f.lines[0] / 2;
    return f;
}

uint8_t* Frame::plane(const VideoFormat& f, int p)
{
    size_t offset = 0;
    for (int i = 0; i < p; ++i)
        offset += f.planeSize(i);
    return data.data() + offset;
}

void FrameQueue::configure(const VideoFormat& format)
{
    std::lock_guard<std::mutex> lock(mutex_);
    format_ = format;
    const size_t total = format.planeSize(0) + format.planeSize(1) + format.planeSize(2);
    for (Slot& s : slots_) {
        s.frame.data.assign(total, 0);
        s.state = State::Free;
    }
    ready_ = -1;
}

VideoFormat FrameQueue::format() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return format_;
}

void FrameQueue::reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (Slot& s : slots_)
        s.state = State::Free;
    ready_ = -1;
}

Frame* FrameQueue::acquireForWrite()
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (Slot& s : slots_) {
        if (s.state == State::Free) {
            s.state = State::Writing;
            return &s.frame;
        }
    }
    // All three busy can only happen if the consumer holds one, one is ready and one is
    // being written: overwrite the ready one, the consumer will just see the newer frame.
    if (ready_ >= 0) {
        Slot& s = slots_[ready_];
        s.state = State::Writing;
        ready_ = -1;
        return &s.frame;
    }
    return nullptr;
}

void FrameQueue::commit(Frame* frame)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < 3; ++i) {
        Slot& s = slots_[i];
        if (&s.frame == frame) {
            if (ready_ >= 0 && ready_ != i)
                slots_[ready_].state = State::Free;   // drop the frame nobody picked up
            s.state = State::Ready;
            ready_ = i;
            return;
        }
    }
}

Frame* FrameQueue::acquireLatest()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (ready_ < 0)
        return nullptr;
    Slot& s = slots_[ready_];
    s.state = State::Reading;
    ready_ = -1;
    return &s.frame;
}

void FrameQueue::release(Frame* frame)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (Slot& s : slots_) {
        if (&s.frame == frame && s.state == State::Reading)
            s.state = State::Free;
    }
}

} // namespace vw
