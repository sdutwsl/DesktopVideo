#include "Mpris.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <sstream>

namespace muralop {
namespace {
constexpr const char* SERVICE = "org.mpris.MediaPlayer2.muralop";
constexpr const char* PATH = "/org/mpris/MediaPlayer2";
constexpr const char* ROOT = "org.mpris.MediaPlayer2";
constexpr const char* PLAYER = "org.mpris.MediaPlayer2.Player";

int64_t microseconds(double seconds)
{
    if (!std::isfinite(seconds) || seconds <= 0) return 0;
    const double maximum = static_cast<double>(std::numeric_limits<int64_t>::max());
    return seconds >= maximum / 1000000 ? std::numeric_limits<int64_t>::max()
                                        : static_cast<int64_t>(seconds * 1000000);
}

std::string fileUri(const std::string& source)
{
    if (source.starts_with("http://") || source.starts_with("https://")) return source;
    std::error_code error;
    const auto absolute = std::filesystem::absolute(std::filesystem::u8path(source), error).generic_u8string();
    if (error) return {};
    std::string result = "file://";
    constexpr char hex[] = "0123456789ABCDEF";
    for (const unsigned char c : absolute) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') result += char(c);
        else { result += '%'; result += hex[c >> 4]; result += hex[c & 15]; }
    }
    return result;
}

std::string uriSource(const std::string& uri)
{
    if (uri.size() > 32768 || uri.find_first_of("\r\n") != std::string::npos || uri.find('\0') != std::string::npos)
        throw sdbus::Error(sdbus::Error::Name("org.freedesktop.DBus.Error.InvalidArgs"), "Invalid media URI");
    if (uri.starts_with("http://") || uri.starts_with("https://")) return uri;
    if (!uri.starts_with("file:"))
        throw sdbus::Error(sdbus::Error::Name("org.mpris.MediaPlayer2.Player.Error.NotSupported"), "Unsupported URI scheme");
    std::string path = uri.substr(5);
    if (path.starts_with("//")) {
        const size_t slash = path.find('/', 2);
        const std::string host = path.substr(2, slash == std::string::npos ? slash : slash - 2);
        if (slash == std::string::npos || (!host.empty() && host != "localhost"))
            throw sdbus::Error(sdbus::Error::Name("org.freedesktop.DBus.Error.InvalidArgs"), "Expected a local file URI");
        path = path.substr(slash);
    }
    if (path.empty() || path.front() != '/')
        throw sdbus::Error(sdbus::Error::Name("org.freedesktop.DBus.Error.InvalidArgs"), "Expected an absolute file URI");
    if (const auto fragment = path.find_first_of("?#"); fragment != std::string::npos) path.resize(fragment);
    const auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string result;
    for (size_t i = 0; i < path.size(); ++i) {
        char c = path[i];
        if (c == '%') {
            if (i + 2 >= path.size() || hex(path[i + 1]) < 0 || hex(path[i + 2]) < 0)
                throw sdbus::Error(sdbus::Error::Name("org.freedesktop.DBus.Error.InvalidArgs"), "Invalid URI escape");
            c = char(hex(path[i + 1]) * 16 + hex(path[i + 2]));
            i += 2;
        }
        if (c == '\0' || c == '\r' || c == '\n')
            throw sdbus::Error(sdbus::Error::Name("org.freedesktop.DBus.Error.InvalidArgs"), "Invalid file URI");
        result += c;
    }
    return result;
}
} // namespace

Mpris::Mpris(EventLoop& loop, sdbus::IConnection& bus, Wallpaper& wallpaper, Actions actions)
    : loop_(loop), bus_(bus), wallpaper_(wallpaper), actions_(std::move(actions))
{
    bus_.requestName(sdbus::ServiceName(SERVICE));
    try {
        object_ = sdbus::createObject(bus_, sdbus::ObjectPath(PATH));
        object_->addVTable(
            sdbus::registerMethod("Raise").implementedAs([this] { if (actions_.raise) actions_.raise(); }),
            sdbus::registerMethod("Quit").implementedAs([this] { if (actions_.quit) actions_.quit(); }),
            sdbus::registerProperty("CanQuit").withGetter([this] { return bool(actions_.quit); }),
            sdbus::registerProperty("CanRaise").withGetter([this] { return bool(actions_.raise); }),
            sdbus::registerProperty("CanSetFullscreen").withGetter([] { return false; }),
            sdbus::registerProperty("Fullscreen").withGetter([] { return false; }).withSetter([](bool) {}),
            sdbus::registerProperty("HasTrackList").withGetter([] { return false; }),
            sdbus::registerProperty("Identity").withGetter([] { return std::string("Muralop"); }),
            sdbus::registerProperty("DesktopEntry").withGetter([] { return std::string("muralop"); }),
            sdbus::registerProperty("SupportedUriSchemes").withGetter([] { return std::vector<std::string>{"file", "https", "http"}; }),
            sdbus::registerProperty("SupportedMimeTypes").withGetter([] {
                return std::vector<std::string>{"video/mp4", "video/webm", "video/x-matroska", "video/quicktime", "video/x-msvideo"};
            })).forInterface(ROOT);

        object_->addVTable(
            sdbus::registerMethod("Play").implementedAs([this] { if (snapshot().canPlay) control("play"); }),
            sdbus::registerMethod("Pause").implementedAs([this] { if (snapshot().canPause) control("pause"); }),
            sdbus::registerMethod("PlayPause").implementedAs([this] {
                if (snapshot().canPause || snapshot().canPlay) control("play-pause");
            }),
            sdbus::registerMethod("Stop").implementedAs([this] { control("stop"); }),
            sdbus::registerMethod("Next").implementedAs([this] { if (snapshot().next) actions_.next(); }),
            sdbus::registerMethod("Previous").implementedAs([this] { if (snapshot().previous) actions_.previous(); }),
            sdbus::registerMethod("Seek").implementedAs([this](int64_t offset) {
                const auto state = snapshot();
                if (!state.canSeek) return;
                const long double target = static_cast<long double>(state.position) + offset;
                if (state.duration && target > state.duration) {
                    if (state.next) actions_.next();
                } else if (target < 0) wallpaper_.seek(0, true);
                else wallpaper_.seek(static_cast<double>(offset) / 1000000, false);
            }),
            sdbus::registerMethod("SetPosition").implementedAs([this](sdbus::ObjectPath track, int64_t position) {
                const auto state = snapshot();
                if (state.canSeek && track == trackId() && position >= 0 && (!state.duration || position <= state.duration))
                    wallpaper_.seek(static_cast<double>(position) / 1000000, true);
            }),
            sdbus::registerMethod("OpenUri").implementedAs([this](std::string uri) {
                auto source = uriSource(uri);
                if (actions_.openUri) actions_.openUri(std::move(source));
            }),
            sdbus::registerSignal("Seeked").withParameters<int64_t>(),
            sdbus::registerProperty("PlaybackStatus").withGetter([this] { return snapshot().status; }),
            sdbus::registerProperty("Metadata").withGetter([this] { return metadata(); }),
            sdbus::registerProperty("Position").withGetter([this] { return snapshot().position; })
                .withUpdateBehavior(sdbus::Flags::EMITS_NO_SIGNAL),
            sdbus::registerProperty("Volume").withGetter([this] { return snapshot().volume; }).withSetter([this](double volume) {
                if (!std::isfinite(volume)) throw sdbus::Error(sdbus::Error::Name("org.freedesktop.DBus.Error.InvalidArgs"), "Invalid volume");
                const int level = static_cast<int>(std::lround(std::clamp(volume, 0.0, 1.0) * 100));
                wallpaper_.setVolume(level);
                if (level > 0) wallpaper_.setMute(false);
                changed("changed");
            }),
            sdbus::registerProperty("Rate").withGetter([] { return 1.0; }).withSetter([this](double rate) {
                if (rate == 0) control("pause");
                else if (rate != 1) throw sdbus::Error(sdbus::Error::Name("org.freedesktop.DBus.Error.NotSupported"), "Only normal playback speed is supported");
            }),
            sdbus::registerProperty("MinimumRate").withGetter([] { return 1.0; }),
            sdbus::registerProperty("MaximumRate").withGetter([] { return 1.0; }),
            sdbus::registerProperty("CanGoNext").withGetter([this] { return snapshot().next; }),
            sdbus::registerProperty("CanGoPrevious").withGetter([this] { return snapshot().previous; }),
            sdbus::registerProperty("CanPlay").withGetter([this] { return snapshot().canPlay; }),
            sdbus::registerProperty("CanPause").withGetter([this] { return snapshot().canPause; }),
            sdbus::registerProperty("CanSeek").withGetter([this] { return snapshot().canSeek; }),
            sdbus::registerProperty("CanControl").withGetter([] { return true; })).forInterface(PLAYER);
        loop_.add(this);
    } catch (...) {
        object_.reset();
        bus_.releaseName(sdbus::ServiceName(SERVICE));
        throw;
    }
}

Mpris::~Mpris()
{
    loop_.remove(this);
    object_.reset();
    try { bus_.releaseName(sdbus::ServiceName(SERVICE)); } catch (const sdbus::Error&) {}
}

Mpris::Snapshot Mpris::snapshot() const
{
    const auto info = wallpaper_.mediaInfo();
    Snapshot state;
    state.source = info.source;
    state.title = info.title;
    state.duration = info.source.empty() ? 0 : microseconds(info.duration);
    state.position = info.source.empty() ? 0 : microseconds(info.position);
    state.seekSerial = info.seekSerial;
    state.revision = info.revision;
    const bool active = !info.source.empty() && (info.playback == "playing" || info.playback == "loading");
    state.status = active ? (wallpaper_.paused() ? "Paused" : "Playing") : "Stopped";
    state.volume = wallpaper_.mute() ? 0 : wallpaper_.volume() / 100.0;
    state.canPlay = wallpaper_.canPlay();
    state.canPause = active;
    state.canSeek = active && info.seekable;
    const bool collection = active && info.source == navigationSource_
        && (navigationMonitor_.empty() || navigationMonitor_ == info.monitor);
    state.next = collection && next_ && bool(actions_.next);
    state.previous = collection && previous_ && bool(actions_.previous);
    return state;
}

sdbus::ObjectPath Mpris::trackId() const
{
    const auto info = wallpaper_.mediaInfo();
    if (info.source.empty()) return sdbus::ObjectPath("/org/mpris/MediaPlayer2/TrackList/NoTrack");
    std::ostringstream id;
    id << "/org/muralop/Tracks/track_" << std::hex << std::hash<std::string>{}(info.source) << '_' << info.revision;
    return sdbus::ObjectPath(id.str());
}

std::map<std::string, sdbus::Variant> Mpris::metadata() const
{
    const auto state = snapshot();
    if (state.source.empty()) return {};
    std::map<std::string, sdbus::Variant> result;
    result.emplace("mpris:trackid", sdbus::Variant(trackId()));
    result.emplace("xesam:url", sdbus::Variant(fileUri(state.source)));
    result.emplace("xesam:title", sdbus::Variant(state.title.empty() ? state.source : state.title));
    if (state.duration > 0) result.emplace("mpris:length", sdbus::Variant(state.duration));
    return result;
}

void Mpris::setNavigation(const std::string& source, bool next, bool previous, const std::string& monitor)
{
    navigationSource_ = source;
    navigationMonitor_ = monitor;
    next_ = next;
    previous_ = previous;
    refresh();
}

void Mpris::changed(const std::string& action)
{
    if (actions_.changed) actions_.changed(action);
    refresh();
}

void Mpris::control(const std::string& action)
{
    // The application must cancel pending resolution before stopping, and
    // serialize stop/play with any in-flight load commands.
    if (actions_.control) { actions_.control(action); return; }
    if (action == "stop") wallpaper_.stop();
    else if (action == "pause" || (action == "play-pause" && snapshot().status == "Playing"))
        wallpaper_.setUserPaused(true);
    else wallpaper_.play();
    changed(action);
}

int Mpris::timeoutMs()
{
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(updateAt_ - std::chrono::steady_clock::now()).count();
    return static_cast<int>(std::clamp<int64_t>(remaining, 1, 500));
}

void Mpris::dispatch(short)
{
    if (std::chrono::steady_clock::now() < updateAt_) return;
    updateAt_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    try { refresh(); }
    catch (const sdbus::Error& error) { fprintf(stderr, "[mpris] %s\n", error.getMessage().c_str()); }
}

void Mpris::refresh()
{
    const auto state = snapshot();
    std::vector<sdbus::PropertyName> properties;
    const auto different = [this](auto member, const auto& value) { return !previousSnapshot_ || (*previousSnapshot_).*member != value; };
    if (different(&Snapshot::status, state.status)) properties.emplace_back("PlaybackStatus");
    if (different(&Snapshot::source, state.source) || different(&Snapshot::revision, state.revision)
        || different(&Snapshot::title, state.title) || different(&Snapshot::duration, state.duration))
        properties.emplace_back("Metadata");
    if (different(&Snapshot::volume, state.volume)) properties.emplace_back("Volume");
    if (different(&Snapshot::canPlay, state.canPlay)) properties.emplace_back("CanPlay");
    if (different(&Snapshot::canPause, state.canPause)) properties.emplace_back("CanPause");
    if (different(&Snapshot::canSeek, state.canSeek)) properties.emplace_back("CanSeek");
    if (different(&Snapshot::next, state.next)) properties.emplace_back("CanGoNext");
    if (different(&Snapshot::previous, state.previous)) properties.emplace_back("CanGoPrevious");
    if (!properties.empty()) object_->emitPropertiesChangedSignal(sdbus::InterfaceName(PLAYER), properties);
    // Position must never be part of PropertiesChanged. Seek discontinuities
    // use the protocol's separate Seeked signal.
    if (previousSnapshot_ && previousSnapshot_->source == state.source && previousSnapshot_->revision == state.revision
        && previousSnapshot_->seekSerial != state.seekSerial)
        object_->emitSignal("Seeked").onInterface(PLAYER).withArguments(state.position);
    previousSnapshot_ = state;
}

} // namespace muralop
