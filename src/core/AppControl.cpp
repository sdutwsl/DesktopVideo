#include "AppControl.h"

#include <charconv>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace muralop {
namespace {
std::string quote(const std::string& text)
{
    std::string out = "\"";
    const char* hex = "0123456789abcdef";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += char(c);
    }
    return out + '"';
}
}

AppControl::AppControl(Wallpaper& wallpaper, Config& config, std::function<void()> quit)
    : wallpaper_(wallpaper), config_(config), quit_(std::move(quit)) {}

std::string AppControl::state() const
{
    std::ostringstream out;
    out << "{\"ok\":true,\"paused\":" << (wallpaper_.userPaused() ? "true" : "false")
        << ",\"mute\":" << (wallpaper_.mute() ? "true" : "false")
        << ",\"volume\":" << wallpaper_.volume()
        << ",\"video\":" << quote(wallpaper_.videoFor(""))
        << ",\"playback\":" << quote(wallpaper_.playbackState())
        << ",\"error\":" << quote(wallpaper_.lastError()) << ",\"monitors\":[";
    bool first = true;
    for (const auto& m : wallpaper_.monitors()) {
        if (!first) out << ',';
        first = false;
        out << "{\"id\":" << quote(m.id) << ",\"width\":" << m.width << ",\"height\":" << m.height
            << ",\"primary\":" << (m.primary ? "true" : "false")
            << ",\"playback\":" << quote(wallpaper_.playbackState(m.id))
            << ",\"video\":" << quote(wallpaper_.videoFor(m.id)) << '}';
    }
    return out.str() + "]}";
}

std::string AppControl::execute(const std::string& request)
{
    try {
        if (request.size() > 32768 || request.find('\0') != std::string::npos || request.find('\r') != std::string::npos)
            throw std::runtime_error("Invalid control request");
        std::vector<std::string> args;
        size_t start = 0;
        for (;;) {
            const auto end = request.find('\n', start);
            args.push_back(request.substr(start, end == std::string::npos ? end : end - start));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        const auto& cmd = args[0];
        if (cmd == "status" && args.size() == 1) {}
        else if (cmd == "set" && args.size() == 3 && !args[1].empty()) {
            if (!args[2].empty()) {
                bool found = false;
                for (const auto& m : wallpaper_.monitors()) found |= m.id == args[2];
                if (!found) throw std::runtime_error("Monitor is no longer connected");
            }
            wallpaper_.setVideo(args[1], args[2]);
        } else if (cmd == "play-resolved" && args.size() == 7) {
            auto online = [](const std::string& url) { return url.starts_with("https://") || url.starts_with("http://"); };
            if (!online(args[1]) || !online(args[2]) || (!args[3].empty() && !online(args[3])))
                throw std::runtime_error("Resolved media must use HTTP or HTTPS");
            if (!args[6].empty()) {
                bool found = false;
                for (const auto& monitor : wallpaper_.monitors()) found |= monitor.id == args[6];
                if (!found) throw std::runtime_error("Monitor is no longer connected");
            }
            wallpaper_.setResolvedVideo({args[1], args[2], args[3], args[4], args[5]}, args[6]);
        } else if (cmd == "stop" && args.size() <= 2) wallpaper_.stopPlayback(args.size() == 2 ? args[1] : "");
        else if (cmd == "clear" && args.size() == 2) wallpaper_.clearOverride(args[1]);
        else if ((cmd == "pause" || cmd == "resume") && args.size() == 1) wallpaper_.setUserPaused(cmd == "pause");
        else if ((cmd == "mute" || cmd == "unmute") && args.size() == 1) wallpaper_.setMute(cmd == "mute");
        else if (cmd == "volume" && args.size() == 2) {
            int value = 0;
            const auto [end, error] = std::from_chars(args[1].data(), args[1].data() + args[1].size(), value);
            if (error != std::errc() || end != args[1].data() + args[1].size() || value < 0 || value > 100)
                throw std::runtime_error("Volume must be between 0 and 100");
            wallpaper_.setVolume(value);
        } else if (cmd == "online" && args.size() == 4) {
            const bool changed = config_.get("ytdlp_path") != args[1] || config_.get("cookies_file") != args[2]
                || config_.get("ytdlp_format") != args[3];
            config_.set("ytdlp_path", args[1]);
            config_.set("cookies_file", args[2]);
            config_.set("ytdlp_format", args[3]);
            if (!config_.save()) throw std::runtime_error("Cannot save online playback settings");
            if (changed) wallpaper_.updateOnlineOptions();
        } else if (cmd == "quit" && args.size() == 1) quit_();
        else throw std::runtime_error("Unknown command or invalid arguments");
        return state();
    } catch (const std::exception& e) {
        return "{\"ok\":false,\"error\":" + quote(e.what()) + '}';
    }
}
} // namespace muralop
