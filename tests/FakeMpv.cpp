// Deterministic player double: exercise the real controller without a GPU or network.
#include "core/MpvPlayer.h"

namespace vw {
MpvPlayer::MpvPlayer(const OnlineOptions&) {}
MpvPlayer::~MpvPlayer() = default;
void MpvPlayer::setOption(const char*, const char*) {}
void MpvPlayer::setWindow(int64_t) {}
void MpvPlayer::setWakeup(std::function<void()> wakeup) { wakeup_ = std::move(wakeup); }
bool MpvPlayer::initialize() { return true; }
void MpvPlayer::processEvents() {}
bool MpvPlayer::open(const std::string& path, bool) { path_ = path; return true; }
void MpvPlayer::stop() { path_.clear(); }
void MpvPlayer::setPaused(bool) {}
void MpvPlayer::setVolume(int) {}
void MpvPlayer::setMute(bool) {}
} // namespace vw
