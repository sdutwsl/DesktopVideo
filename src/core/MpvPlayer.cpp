#include "MpvPlayer.h"

#include <mpv/client.h>

#include <cstdio>
#include <cstdlib>
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
    if (!online.format.empty()) setOption("ytdl-format", online.format.c_str());
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
        if (items.empty()) return;
        mpv_node_list list{static_cast<int>(items.size()), items.data(), keys.data()};
        mpv_node node{};
        node.format = MPV_FORMAT_NODE_MAP;
        node.u.list = &list;
        if (mpv_set_option(mpv_, option, MPV_FORMAT_NODE, &node) < 0)
            lastError_ = std::string("Cannot configure ") + option;
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
    mpv_request_log_messages(mpv_, getenv("MURALOP_DEBUG") ? "info" : "error");
    // Registered once; setWakeup() only swaps the target, so we never call into mpv
    // while holding wakeupMutex_ (mpv holds its own lock while invoking the callback).
    mpv_set_wakeup_callback(mpv_, &MpvPlayer::wakeupCb, this);
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
        if (ev->event_id == MPV_EVENT_LOG_MESSAGE) {
            auto* msg = static_cast<mpv_event_log_message*>(ev->data);
            fprintf(stderr, "[mpv/%s] %s", msg->prefix, msg->text);
        } else if (ev->event_id == MPV_EVENT_END_FILE) {
            auto* end = static_cast<mpv_event_end_file*>(ev->data);
            if (end->reason == MPV_END_FILE_REASON_ERROR) {
                lastError_ = mpv_error_string(end->error);
                fprintf(stderr, "[mpv] cannot play %s: %s\n", path_.c_str(), mpv_error_string(end->error));
            }
        }
    }
}

bool MpvPlayer::open(const std::string& path, bool withAudio)
{
    if (!mpv_)
        return false;
    // Only one monitor's player should be heard.
    mpv_set_property_string(mpv_, "aid", withAudio ? "auto" : "no");
    const char* cmd[] = {"loadfile", path.c_str(), nullptr};
    path_ = path;
    lastError_.clear();
    return mpv_command(mpv_, cmd) >= 0;
}

void MpvPlayer::stop()
{
    if (mpv_) {
        const char* cmd[] = {"stop", nullptr};
        mpv_command(mpv_, cmd);
    }
    path_.clear();
}

void MpvPlayer::setPaused(bool paused)
{
    if (mpv_)
        mpv_set_property_string(mpv_, "pause", paused ? "yes" : "no");
}

void MpvPlayer::setVolume(int volume)
{
    if (!mpv_)
        return;
    double v = volume;
    mpv_set_property(mpv_, "volume", MPV_FORMAT_DOUBLE, &v);
}

void MpvPlayer::setMute(bool mute)
{
    if (mpv_)
        mpv_set_property_string(mpv_, "mute", mute ? "yes" : "no");
}

} // namespace muralop
