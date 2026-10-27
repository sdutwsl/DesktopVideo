#include "Wallpaper.h"

#include <algorithm>

namespace muralop {

static const char* KEY_VIDEO = "video";
static const char* KEY_VOLUME = "volume";
static const char* KEY_MUTE = "mute";
static const char* KEY_PAUSE_LOCKED = "pause_when_locked";
static const char* KEY_AUDIO_MONITOR = "audio_monitor";

static std::string overrideKey(const std::string& monitorId) { return std::string(KEY_VIDEO) + "." + monitorId; }

Wallpaper::Wallpaper(Platform& platform, Config& config)
    : platform_(platform), config_(config)
{
}

Wallpaper::~Wallpaper()
{
    shutdown();
}

std::string Wallpaper::audioMonitorId(const std::vector<MonitorInfo>& monitors) const
{
    const std::string wanted = config_.get(KEY_AUDIO_MONITOR);
    for (const MonitorInfo& m : monitors)
        if (!wanted.empty() && m.id == wanted)
            return m.id;
    for (const MonitorInfo& m : monitors)
        if (m.primary)
            return m.id;
    return monitors.empty() ? "" : monitors.front().id;
}

void Wallpaper::refreshMonitors()
{
    const std::vector<MonitorInfo> current = platform_.monitors();
    const std::string audioId = audioMonitorId(current);

    auto same = [](const MonitorInfo& a, const MonitorInfo& b) {
        return a.id == b.id && a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    };

    // Drop slots whose monitor vanished, moved, resized or changed audio role.
    slots_.erase(std::remove_if(slots_.begin(), slots_.end(), [&](const std::unique_ptr<Slot>& s) {
        auto it = std::find_if(current.begin(), current.end(), [&](const MonitorInfo& m) { return same(m, s->monitor); });
        return it == current.end() || (it->id == audioId) != s->audio;
    }), slots_.end());

    // Create slots for monitors we do not have yet.
    for (const MonitorInfo& m : current) {
        if (find(m.id))
            continue;
        auto slot = std::make_unique<Slot>();
        slot->monitor = m;
        slot->audio = (m.id == audioId);
        slot->surface = platform_.createSurface(m);
        if (!slot->surface)
            continue;
        slot->player = std::make_unique<MpvPlayer>(OnlineOptions{
            config_.get("ytdlp_path"), config_.get("cookies_file"), config_.get("ytdlp_format")});
        slot->surface->attach(*slot->player);
        openSlot(*slot);
        slots_.push_back(std::move(slot));
    }
    applyPlayback();
}

void Wallpaper::rebuildSurfaces()
{
    slots_.clear();
    refreshMonitors();
}

void Wallpaper::updateOnlineOptions()
{
    const OnlineOptions online{config_.get("ytdlp_path"), config_.get("cookies_file"), config_.get("ytdlp_format")};
    for (auto& slot : slots_)
        slot->player->setOnlineOptions(online);
}

void Wallpaper::openSlot(Slot& slot)
{
    const std::string path = videoFor(slot.monitor.id);
    if (path.empty()) {
        slot.player->stop();
        return;
    }
    // Stream addresses resolved by the UI expire after a few hours. Without a
    // fresh one (e.g. at login, before the UI runs) mpv's ytdl hook resolves the
    // original address itself; loadfile is asynchronous either way.
    const auto resolved = resolved_.find(hasOverride(slot.monitor.id) ? slot.monitor.id : "");
    if (resolved != resolved_.end() && resolved->second.media.source == path
        && std::chrono::steady_clock::now() - resolved->second.at < std::chrono::hours(4))
        slot.player->openResolved(resolved->second.media, slot.audio);
    else
        slot.player->open(path, slot.audio);
    slot.player->setVolume(volume());
    slot.player->setMute(mute());
}

void Wallpaper::setVideo(const std::string& path, const std::string& monitorId)
{
    resolved_.erase(monitorId);
    if (monitorId.empty())
        config_.set(KEY_VIDEO, path);
    else
        config_.set(overrideKey(monitorId), path);
    config_.save();

    for (auto& slot : slots_) {
        const bool affected = monitorId.empty() ? !hasOverride(slot->monitor.id) : slot->monitor.id == monitorId;
        if (affected)
            openSlot(*slot);
    }
    applyPlayback();
}

void Wallpaper::setResolvedVideo(const ResolvedMedia& media, const std::string& monitorId)
{
    // Only the original address is persisted; signed stream addresses stay in memory.
    config_.set(monitorId.empty() ? KEY_VIDEO : overrideKey(monitorId), media.source);
    resolved_[monitorId] = {media, std::chrono::steady_clock::now()};
    config_.save();
    for (auto& slot : slots_) {
        if (monitorId.empty() ? !hasOverride(slot->monitor.id) : slot->monitor.id == monitorId)
            openSlot(*slot);
    }
    applyPlayback();
}

void Wallpaper::stopPlayback(const std::string& monitorId)
{
    for (auto& slot : slots_)
        if (monitorId.empty() ? !hasOverride(slot->monitor.id) : slot->monitor.id == monitorId)
            slot->player->stop();
}

std::string Wallpaper::playbackState(const std::string& monitorId) const
{
    bool playing = false;
    bool loading = false;
    for (const auto& slot : slots_) {
        if (!monitorId.empty() && slot->monitor.id != monitorId) continue;
        const auto& state = slot->player->playbackState();
        if (state == "error") return "error";
        loading |= state == "loading";
        playing |= state == "playing";
    }
    return loading ? "loading" : playing ? "playing" : "idle";
}

void Wallpaper::clearOverride(const std::string& monitorId)
{
    if (!hasOverride(monitorId))
        return;
    config_.erase(overrideKey(monitorId));
    resolved_.erase(monitorId);
    config_.save();
    if (Slot* slot = find(monitorId))
        openSlot(*slot);
    applyPlayback();
}

std::string Wallpaper::videoFor(const std::string& monitorId) const
{
    const std::string specific = config_.get(overrideKey(monitorId));
    return specific.empty() ? config_.get(KEY_VIDEO) : specific;
}

bool Wallpaper::hasOverride(const std::string& monitorId) const
{
    return !config_.get(overrideKey(monitorId)).empty();
}

void Wallpaper::setUserPaused(bool paused)
{
    userPaused_ = paused;
    applyPlayback();
}

void Wallpaper::setLocked(bool locked)
{
    locked_ = locked;
    applyPlayback();
}

void Wallpaper::setSuspended(bool suspended)
{
    suspended_ = suspended;
    applyPlayback();
}

void Wallpaper::applyPlayback()
{
    const bool pauseLocked = config_.getBool(KEY_PAUSE_LOCKED, true);
    const bool pause = userPaused_ || suspended_ || (pauseLocked && locked_);
    for (auto& slot : slots_) {
        if (slot->player->path().empty())
            continue;
        slot->player->setPaused(pause);
    }
}

void Wallpaper::setVolume(int volume)
{
    volume = std::clamp(volume, 0, 100);
    config_.set(KEY_VOLUME, volume);
    config_.save();
    for (auto& slot : slots_)
        slot->player->setVolume(volume);
}

int Wallpaper::volume() const
{
    return std::clamp(config_.getInt(KEY_VOLUME, 50), 0, 100);
}

void Wallpaper::setMute(bool mute)
{
    config_.set(KEY_MUTE, mute);
    config_.save();
    for (auto& slot : slots_)
        slot->player->setMute(mute);
}

bool Wallpaper::mute() const
{
    return config_.getBool(KEY_MUTE, false);
}

std::vector<MonitorInfo> Wallpaper::monitors() const
{
    std::vector<MonitorInfo> out;
    for (const auto& slot : slots_)
        out.push_back(slot->monitor);
    return out;
}

void Wallpaper::shutdown()
{
    slots_.clear();
}

std::string Wallpaper::lastError() const
{
    for (const auto& slot : slots_)
        if (!slot->player->lastError().empty())
            return slot->monitor.id + ": " + slot->player->lastError();
    return {};
}

Wallpaper::Slot* Wallpaper::find(const std::string& monitorId)
{
    for (auto& slot : slots_)
        if (slot->monitor.id == monitorId)
            return slot.get();
    return nullptr;
}

} // namespace muralop
