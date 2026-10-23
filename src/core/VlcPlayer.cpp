#include "VlcPlayer.h"
#include "FrameQueue.h"

#include <vlc/vlc.h>

#include <cstring>

namespace vw {

// ---- VlcInstance -----------------------------------------------------------

VlcInstance::VlcInstance(const std::vector<std::string>& args)
{
    std::vector<const char*> argv;
    argv.reserve(args.size());
    for (const std::string& a : args)
        argv.push_back(a.c_str());
    instance_ = libvlc_new(int(argv.size()), argv.data());
}

VlcInstance::~VlcInstance()
{
    if (instance_)
        libvlc_release(instance_);
}

// ---- VlcPlayer -------------------------------------------------------------

VlcPlayer::VlcPlayer(VlcInstance& instance) : instance_(instance)
{
    player_ = libvlc_media_player_new(instance.get());
    // The wallpaper never wants VLC to react to input or show OSD.
    libvlc_video_set_mouse_input(player_, 0);
    libvlc_video_set_key_input(player_, 0);
}

VlcPlayer::~VlcPlayer()
{
    stop();
    libvlc_media_player_release(player_);
}

void VlcPlayer::setNativeWindow(void* hwnd)
{
    sink_ = nullptr;
    libvlc_video_set_callbacks(player_, nullptr, nullptr, nullptr, nullptr);
    libvlc_media_player_set_hwnd(player_, hwnd);
}

void VlcPlayer::setFrameSink(FrameSink* sink)
{
    sink_ = sink;
    libvlc_video_set_format_callbacks(player_, &VlcPlayer::formatCb, &VlcPlayer::cleanupCb);
    libvlc_video_set_callbacks(player_, &VlcPlayer::lockCb, &VlcPlayer::unlockCb, &VlcPlayer::displayCb, this);
}

void VlcPlayer::setFillAspect(int width, int height)
{
    cropAspect_ = std::to_string(width) + ":" + std::to_string(height);
    if (!sink_) {
        libvlc_video_set_crop_geometry(player_, cropAspect_.c_str());
        libvlc_video_set_scale(player_, 0);   // 0 = fit the window
    }
}

bool VlcPlayer::open(const std::string& path, bool withAudio)
{
    stop();
    libvlc_media_t* media = libvlc_media_new_path(instance_.get(), path.c_str());
    if (!media)
        return false;
    // Loop by repeating the input; EndReached callbacks are racy for seamless loops.
    libvlc_media_add_option(media, ":input-repeat=65535");
    if (!withAudio)
        libvlc_media_add_option(media, ":no-audio");
    libvlc_media_player_set_media(player_, media);
    libvlc_media_release(media);   // the player holds its own reference
    path_ = path;

    if (!cropAspect_.empty() && !sink_)
        libvlc_video_set_crop_geometry(player_, cropAspect_.c_str());
    return libvlc_media_player_play(player_) == 0;
}

void VlcPlayer::stop()
{
    if (player_)
        libvlc_media_player_stop(player_);
    path_.clear();
}

void VlcPlayer::setPaused(bool paused)
{
    libvlc_media_player_set_pause(player_, paused ? 1 : 0);
}

bool VlcPlayer::playing() const
{
    return libvlc_media_player_is_playing(player_) != 0;
}

void VlcPlayer::setVolume(int volume)
{
    libvlc_audio_set_volume(player_, volume);
}

void VlcPlayer::setMute(bool mute)
{
    libvlc_audio_set_mute(player_, mute ? 1 : 0);
}

// ---- libvlc "vmem" callbacks, called from VLC's threads ----------------------

unsigned VlcPlayer::formatCb(void** opaque, char* chroma, unsigned* width, unsigned* height,
                             unsigned* pitches, unsigned* lines)
{
    auto* self = static_cast<VlcPlayer*>(*opaque);
    std::memcpy(chroma, "I420", 4);
    VideoFormat f = VideoFormat::i420(*width, *height);
    for (int p = 0; p < 3; ++p) {
        pitches[p] = f.pitch[p];
        lines[p] = f.lines[p];
    }
    if (self->sink_)
        self->sink_->setup(f);
    return 1;   // number of pictures VLC may have locked at once
}

void VlcPlayer::cleanupCb(void* opaque)
{
    auto* self = static_cast<VlcPlayer*>(opaque);
    if (self->sink_)
        self->sink_->cleanup();
}

void* VlcPlayer::lockCb(void* opaque, void** planes)
{
    auto* self = static_cast<VlcPlayer*>(opaque);
    return self->sink_ ? self->sink_->lock(planes) : nullptr;
}

void VlcPlayer::unlockCb(void* opaque, void* picture, void* const*)
{
    auto* self = static_cast<VlcPlayer*>(opaque);
    if (self->sink_)
        self->sink_->unlock(picture);
}

void VlcPlayer::displayCb(void* opaque, void* picture)
{
    auto* self = static_cast<VlcPlayer*>(opaque);
    if (self->sink_)
        self->sink_->display(picture);
}

} // namespace vw
