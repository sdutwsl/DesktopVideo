// Deterministic player double: exercise the real controller without a GPU or network.
#include "core/MpvPlayer.h"

namespace muralop {
MpvPlayer::MpvPlayer(const OnlineOptions&) {}
MpvPlayer::~MpvPlayer() = default;
void MpvPlayer::setOption(const char*, const char*) {}
void MpvPlayer::setWindow(int64_t) {}
void MpvPlayer::setWakeup(std::function<void()> wakeup) { wakeup_ = std::move(wakeup); }
bool MpvPlayer::initialize() { return true; }
void MpvPlayer::setOnlineOptions(const OnlineOptions&) {}
void MpvPlayer::processEvents() {}
bool MpvPlayer::open(const std::string& path, bool) { path_ = path; title_.clear(); ++loadRequest_; playbackState_ = "loading"; return true; }
bool MpvPlayer::openResolved(const ResolvedMedia& media, bool audio) { const bool result = open(media.source, audio); title_ = media.title; return result; }
void MpvPlayer::stop() { path_.clear(); playbackState_ = "idle"; }
void MpvPlayer::setPaused(bool paused) { paused_ = paused; }
void MpvPlayer::seek(double seconds, bool absolute) { position_ = absolute ? seconds : position_ + seconds; }
MediaInfo MpvPlayer::mediaInfo() const
{
    MediaInfo info{path_, title_, position_, duration_, paused_, seekable_, seekSerial_, playbackState_};
    info.revision = loadRequest_;
    return info;
}
void MpvPlayer::setVolume(int) {}
void MpvPlayer::setMute(bool) {}
} // namespace muralop
