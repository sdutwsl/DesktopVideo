#include "MpvPlayer.h"

#include <mpv/client.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <vector>

namespace muralop {

MpvPlayer::MpvPlayer(const OnlineOptions& online)
{
    mpv_ = mpv_create();
    if (!mpv_)
        return;
    // A wallpaper: loop forever, fill the screen (crop instead of letterbox), never
    // react to input or draw OSD, and do not read the user's mpv.conf.
    setOption("config", "no");
    setOption("load-scripts", "no");
    setOption("ytdl", "yes");
    setOnlineOptions(online);
    setOption("idle", "yes");
    setOption("loop-file", "inf");
    setOption("keepaspect", "yes");
    setOption("panscan", "1.0");
    setOption("hwdec", "auto-safe");
    setOption("osc", "no");
    setOption("osd-level", "0");
    setOption("input-default-bindings", "no");
    setOption("input-vo-keyboard", "no");
    setOption("input-cursor", "no");
    setOption("cursor-autohide", "no");
    setOption("stop-screensaver", "no");
    setOption("audio-display", "no");
    setOption("terminal", "no");
    // Stalled connections to video CDNs must fail fast so a retry can follow, and
    // a long-running wallpaper should resume after a dropped connection.
    setOption("network-timeout", "10");
    setOption("stream-lavf-o", "reconnect=1,reconnect_on_network_error=1,reconnect_delay_max=5");
    mpv_request_log_messages(mpv_, getenv("MURALOP_DEBUG") ? "info" : "error");
    // Do not call into mpv while holding wakeupMutex_: mpv holds its core lock
    // while invoking the callback.
    mpv_set_wakeup_callback(mpv_, &MpvPlayer::wakeupCb, this);
}

void MpvPlayer::setOnlineOptions(const OnlineOptions& online)
{
    if (!mpv_) return;
    // Updating extractor settings must never destroy a player that is loading a
    // network stream. Queue runtime changes on mpv's core thread instead.
    auto apply = [&](const char* name, mpv_format format, void* value) {
        const std::string property = std::string("options/") + name;
        const int result = initialized_
            ? mpv_set_property_async(mpv_, 0, property.c_str(), format, value)
            : mpv_set_option(mpv_, name, format, value);
        if (result < 0) lastError_ = std::string("Cannot configure ") + name;
    };
    const char* format = online.format.c_str();
    apply("ytdl-format", MPV_FORMAT_STRING, &format);
    // Use typed map options so commas, spaces and Windows drive letters in paths
    // are never interpreted as mpv's option-list separators.
    auto setMap = [&](const char* option, std::vector<std::pair<const char*, std::string>> entries) {
        std::vector<char*> keys;
        std::vector<mpv_node> items;
        for (auto& [key, value] : entries) {
            if (value.empty()) continue;
            keys.push_back(const_cast<char*>(key));
            mpv_node item{};
            item.format = MPV_FORMAT_STRING;
            item.u.string = value.data();
            items.push_back(item);
        }
        mpv_node_list list{static_cast<int>(items.size()), items.data(), keys.data()};
        mpv_node node{};
        node.format = MPV_FORMAT_NODE_MAP;
        node.u.list = &list;
        apply(option, MPV_FORMAT_NODE, &node);
    };
    setMap("script-opts", {{"ytdl_hook-ytdl_path", online.extractorPath}});
    std::string jsRuntime;
    if (!online.extractorPath.empty()) {
        std::error_code error;
        auto deno = std::filesystem::u8path(online.extractorPath).parent_path() / "deno";
        if (!std::filesystem::is_regular_file(deno, error)) deno += ".exe";
        if (std::filesystem::is_regular_file(deno, error)) {
            const auto path = deno.generic_u8string();
            jsRuntime = "deno:" + std::string(reinterpret_cast<const char*>(path.data()), path.size());
        }
    }
    setMap("ytdl-raw-options", {{"cookies", online.cookieFile}, {"js-runtimes", jsRuntime}});
}

MpvPlayer::~MpvPlayer()
{
    setWakeup(nullptr);
    if (mpv_)
        mpv_terminate_destroy(mpv_);
}

void MpvPlayer::setOption(const char* name, const char* value)
{
    if (mpv_ && mpv_set_option_string(mpv_, name, value) < 0)
        fprintf(stderr, "[mpv] option %s=%s rejected\n", name, value);
}

void MpvPlayer::setWindow(int64_t wid)
{
    if (mpv_)
        mpv_set_option(mpv_, "wid", MPV_FORMAT_INT64, &wid);
}

void MpvPlayer::setWakeup(std::function<void()> wakeup)
{
    std::lock_guard<std::mutex> lock(wakeupMutex_);
    wakeup_ = std::move(wakeup);
}

void MpvPlayer::wakeupCb(void* opaque)
{
    auto* self = static_cast<MpvPlayer*>(opaque);
    std::lock_guard<std::mutex> lock(self->wakeupMutex_);
    if (self->wakeup_)
        self->wakeup_();
}

bool MpvPlayer::initialize()
{
    if (!mpv_)
        return false;
    const int err = mpv_initialize(mpv_);
    if (err < 0) {
        lastError_ = mpv_error_string(err);
        fprintf(stderr, "[mpv] initialisation failed: %s\n", mpv_error_string(err));
        return false;
    }
    initialized_ = true;
    // Observe on mpv's core thread; MPRIS getters never block the render loop
    // with synchronous property reads.
    mpv_observe_property(mpv_, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv_, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv_, 0, "seekable", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv_, 0, "media-title", MPV_FORMAT_STRING);
    return true;
}

void MpvPlayer::processEvents()
{
    if (!mpv_)
        return;
    for (;;) {
        mpv_event* ev = mpv_wait_event(mpv_, 0);
        if (ev->event_id == MPV_EVENT_NONE)
            break;
        if (ev->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            auto* property = static_cast<mpv_event_property*>(ev->data);
            if (!property->data) continue;
            if (!strcmp(property->name, "time-pos") && property->format == MPV_FORMAT_DOUBLE) {
                position_ = *static_cast<double*>(property->data);
                if (seeking_) { ++seekSerial_; seeking_ = false; }
            } else if (!strcmp(property->name, "duration") && property->format == MPV_FORMAT_DOUBLE)
                duration_ = *static_cast<double*>(property->data);
            else if (!strcmp(property->name, "pause") && property->format == MPV_FORMAT_FLAG)
                paused_ = *static_cast<int*>(property->data) != 0;
            else if (!strcmp(property->name, "seekable") && property->format == MPV_FORMAT_FLAG)
                seekable_ = *static_cast<int*>(property->data) != 0;
            else if (!strcmp(property->name, "media-title") && property->format == MPV_FORMAT_STRING) {
                const char* title = *static_cast<char**>(property->data);
                mediaTitle_ = title ? title : "";
            }
        } else if (ev->event_id == MPV_EVENT_SEEK) {
            seeking_ = true;
        } else if (ev->event_id == MPV_EVENT_LOG_MESSAGE) {
            auto* msg = static_cast<mpv_event_log_message*>(ev->data);
            fprintf(stderr, "[mpv/%s] %s", msg->prefix, msg->text);
        } else if (ev->event_id == MPV_EVENT_SET_PROPERTY_REPLY && ev->error < 0) {
            fprintf(stderr, "[mpv] cannot set property: %s\n", mpv_error_string(ev->error));
        } else if (ev->event_id == MPV_EVENT_COMMAND_REPLY && ev->reply_userdata == loadRequest_) {
            // Events of the file being replaced may still be queued; only the
            // playlist entry created by the latest loadfile reports progress.
            if (ev->error < 0) {
                lastError_ = mpv_error_string(ev->error);
                playbackState_ = "error";
            } else {
                const mpv_node& result = static_cast<mpv_event_command*>(ev->data)->result;
                if (result.format == MPV_FORMAT_NODE_MAP)
                    for (int i = 0; i < result.u.list->num; ++i)
                        if (!strcmp(result.u.list->keys[i], "playlist_entry_id") && result.u.list->values[i].format == MPV_FORMAT_INT64)
                            loadEntry_ = result.u.list->values[i].u.int64;
            }
        } else if (ev->event_id == MPV_EVENT_START_FILE) {
            currentEntry_ = static_cast<mpv_event_start_file*>(ev->data)->playlist_entry_id;
        } else if (ev->event_id == MPV_EVENT_PLAYBACK_RESTART && !path_.empty() && loadEntry_ >= 0 && currentEntry_ == loadEntry_) {
            playbackState_ = "playing";
            retries_ = 0;
        } else if (ev->event_id == MPV_EVENT_END_FILE) {
            auto* end = static_cast<mpv_event_end_file*>(ev->data);
            if (end->reason == MPV_END_FILE_REASON_ERROR && end->playlist_entry_id == loadEntry_) {
                const bool online = path_.starts_with("https://") || path_.starts_with("http://");
                fprintf(stderr, "[mpv] cannot play %s: %s\n", path_.c_str(), mpv_error_string(end->error));
                // Connections to video CDNs fail intermittently; try again before giving up.
                if (online && retry_ && retries_ < MAX_RETRIES) {
                    ++retries_;
                    fprintf(stderr, "[mpv] retrying (%d/%d)\n", retries_, MAX_RETRIES);
                    retry_();
                } else {
                    lastError_ = mpv_error_string(end->error);
                    playbackState_ = "error";
                }
            }
        }
    }
}

bool MpvPlayer::open(const std::string& path, bool withAudio)
{
    title_.clear();
    retries_ = 0;
    retry_ = [this, path, withAudio] { loadPath(path, withAudio); };
    return loadPath(path, withAudio);
}

bool MpvPlayer::openResolved(const ResolvedMedia& media, bool withAudio)
{
    title_ = media.title;
    retries_ = 0;
    // The last attempt lets mpv's ytdl hook resolve the original address again,
    // in case the signed stream addresses have expired.
    retry_ = [this, media, withAudio] {
        if (retries_ < MAX_RETRIES) loadResolved(media, withAudio);
        else loadPath(media.source, withAudio);
    };
    return loadResolved(media, withAudio);
}

bool MpvPlayer::loadPath(const std::string& path, bool withAudio)
{
    if (!mpv_)
        return false;
    // Only one monitor's player should be heard.
    const char* aid = withAudio ? "auto" : "no";
    mpv_set_property_async(mpv_, 0, "aid", MPV_FORMAT_STRING, &aid);
    const char* cmd[] = {"loadfile", path.c_str(), nullptr};
    path_ = path;
    position_ = duration_ = 0;
    seekable_ = seeking_ = false;
    mediaTitle_.clear();
    lastError_.clear();
    playbackState_ = "loading";
    loadEntry_ = -1;
    return mpv_command_async(mpv_, ++loadRequest_, cmd) >= 0;
}

bool MpvPlayer::loadResolved(const ResolvedMedia& media, bool withAudio)
{
    if (!mpv_) return false;
    // File-local options keep signed streams out of persistent settings. The
    // resolver runs outside this process; libmpv only receives playable media.
    std::vector<std::pair<const char*, std::string>> values = {
        {"ytdl", "no"}, {"aid", withAudio ? "auto" : "no"},
        {"user-agent", media.userAgent}, {"referrer", media.referrer},
    };
    if (withAudio && !media.audio.empty()) values.emplace_back("audio-file", media.audio);
    std::vector<char*> keys;
    std::vector<mpv_node> options;
    for (auto& [key, value] : values) {
        keys.push_back(const_cast<char*>(key));
        mpv_node node{};
        node.format = MPV_FORMAT_STRING;
        node.u.string = value.data();
        options.push_back(node);
    }
    mpv_node_list optionList{static_cast<int>(options.size()), options.data(), keys.data()};
    mpv_node args[5]{};
    const char* strings[] = {"loadfile", media.video.c_str(), "replace"};
    for (int i = 0; i < 3; ++i) {
        args[i].format = MPV_FORMAT_STRING;
        args[i].u.string = const_cast<char*>(strings[i]);
    }
    args[3].format = MPV_FORMAT_INT64;
    args[3].u.int64 = -1;
    args[4].format = MPV_FORMAT_NODE_MAP;
    args[4].u.list = &optionList;
    mpv_node_list arguments{5, args, nullptr};
    mpv_node command{};
    command.format = MPV_FORMAT_NODE_ARRAY;
    command.u.list = &arguments;
    path_ = media.source;
    position_ = duration_ = 0;
    seekable_ = seeking_ = false;
    mediaTitle_.clear();
    lastError_.clear();
    playbackState_ = "loading";
    loadEntry_ = -1;
    const int result = mpv_command_node_async(mpv_, ++loadRequest_, &command);
    if (result < 0) {
        lastError_ = mpv_error_string(result);
        playbackState_ = "error";
    }
    return result >= 0;
}

void MpvPlayer::stop()
{
    if (mpv_) {
        const char* cmd[] = {"stop", nullptr};
        mpv_command_async(mpv_, 0, cmd);
    }
    path_.clear();
    title_.clear();
    mediaTitle_.clear();
    position_ = duration_ = 0;
    seekable_ = seeking_ = false;
    lastError_.clear();
    playbackState_ = "idle";
    loadEntry_ = -1;
    retry_ = nullptr;
}

void MpvPlayer::setPaused(bool paused)
{
    if (mpv_) {
        int value = paused;
        mpv_set_property_async(mpv_, 0, "pause", MPV_FORMAT_FLAG, &value);
    }
}

void MpvPlayer::setVolume(int volume)
{
    if (!mpv_)
        return;
    double v = volume;
    mpv_set_property_async(mpv_, 0, "volume", MPV_FORMAT_DOUBLE, &v);
}

MediaInfo MpvPlayer::mediaInfo() const
{
    MediaInfo info{path_, title_.empty() ? mediaTitle_ : title_, position_, duration_, paused_, seekable_, seekSerial_, playbackState_};
    info.revision = loadRequest_;
    return info;
}

void MpvPlayer::seek(double seconds, bool absolute)
{
    if (!mpv_ || !seekable_ || path_.empty() || !std::isfinite(seconds)) return;
    if (absolute && seconds < 0) return;
    if (absolute && duration_ > 0 && seconds > duration_) return;
    mpv_node items[3]{};
    items[0].format = MPV_FORMAT_STRING;
    items[0].u.string = const_cast<char*>("seek");
    items[1].format = MPV_FORMAT_DOUBLE;
    items[1].u.double_ = seconds;
    items[2].format = MPV_FORMAT_STRING;
    items[2].u.string = const_cast<char*>(absolute ? "absolute+exact" : "relative+exact");
    mpv_node_list list{3, items, nullptr};
    mpv_node command{};
    command.format = MPV_FORMAT_NODE_ARRAY;
    command.u.list = &list;
    mpv_command_node_async(mpv_, 0, &command);
}

void MpvPlayer::setMute(bool mute)
{
    if (mpv_) {
        int value = mute;
        mpv_set_property_async(mpv_, 0, "mute", MPV_FORMAT_FLAG, &value);
    }
}

} // namespace muralop
