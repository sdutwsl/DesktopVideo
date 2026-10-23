#include "Session.h"

#include <cstdio>
#include <map>

namespace vw {

// ---- SessionMonitor ---------------------------------------------------------------

SessionMonitor::SessionMonitor(sdbus::IConnection& sessionBus, sdbus::IConnection* systemBus)
{
    auto lockHandler = [this](sdbus::Message& msg) {
        bool active = false;
        msg >> active;
        if (onLocked)
            onLocked(active);
    };
    // KDE and most others speak the freedesktop interface, GNOME its own; both carry the same bool.
    matches_.push_back(sessionBus.addMatch(
        "type='signal',interface='org.freedesktop.ScreenSaver',member='ActiveChanged'", lockHandler));
    matches_.push_back(sessionBus.addMatch(
        "type='signal',interface='org.gnome.ScreenSaver',member='ActiveChanged'", lockHandler));

    if (!systemBus)
        return;
    matches_.push_back(systemBus->addMatch(
        "type='signal',sender='org.freedesktop.login1',interface='org.freedesktop.login1.Manager',member='PrepareForSleep'",
        [this](sdbus::Message& msg) {
            bool sleeping = false;
            msg >> sleeping;
            if (onSleep)
                onSleep(sleeping);
        }));
    matches_.push_back(systemBus->addMatch(
        "type='signal',sender='org.freedesktop.login1',interface='org.freedesktop.login1.Manager',member='PrepareForShutdown'",
        [this](sdbus::Message& msg) {
            bool shuttingDown = false;
            msg >> shuttingDown;
            if (shuttingDown && onShutdown)
                onShutdown();
        }));
}

// ---- FileChooser ------------------------------------------------------------------

static std::string uriToPath(const std::string& uri)
{
    static const std::string prefix = "file://";
    if (uri.compare(0, prefix.size(), prefix) != 0)
        return uri;
    std::string out;
    for (size_t i = prefix.size(); i < uri.size(); ++i) {
        if (uri[i] == '%' && i + 2 < uri.size()) {
            out += char(std::stoi(uri.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += uri[i];
        }
    }
    return out;
}

FileChooser::FileChooser(sdbus::IConnection& sessionBus) : bus_(sessionBus)
{
    portal_ = sdbus::createProxy(bus_, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop");
}

void FileChooser::open(const std::string& title, std::function<void(std::string)> onDone)
{
    // The portal answers with a Response signal on a request path derived from our
    // unique name and the token we pass, so we can subscribe before asking.
    const std::string token = "vw" + std::to_string(++counter_);
    std::string sender = bus_.getUniqueName().substr(1);   // drop the leading ':'
    for (char& c : sender)
        if (c == '.')
            c = '_';
    const std::string requestPath = "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;

    response_ = bus_.addMatch(
        "type='signal',interface='org.freedesktop.portal.Request',member='Response',path='" + requestPath + "'",
        [onDone](sdbus::Message& msg) {
            uint32_t code = 1;
            std::map<std::string, sdbus::Variant> results;
            msg >> code >> results;
            std::string chosen;
            if (code == 0) {
                auto it = results.find("uris");
                if (it != results.end()) {
                    auto uris = it->second.get<std::vector<std::string>>();
                    if (!uris.empty())
                        chosen = uriToPath(uris.front());
                }
            }
            onDone(chosen);
        });

    using Filter = sdbus::Struct<std::string, std::vector<sdbus::Struct<uint32_t, std::string>>>;
    std::vector<Filter> filters = {
        Filter("视频", {sdbus::Struct<uint32_t, std::string>(1u, "video/*")}),
        Filter("所有文件", {sdbus::Struct<uint32_t, std::string>(0u, "*")}),
    };
    std::map<std::string, sdbus::Variant> options = {
        {"handle_token", sdbus::Variant(token)},
        {"filters", sdbus::Variant(filters)},
    };

    portal_->callMethodAsync("OpenFile")
        .onInterface("org.freedesktop.portal.FileChooser")
        .withArguments(std::string(), title, options)
        .uponReplyInvoke([onDone](const sdbus::Error* error, sdbus::ObjectPath) {
            if (error) {
                fprintf(stderr, "[portal] file chooser unavailable: %s\n", error->getMessage().c_str());
                onDone("");
            }
        });
}

// ---- Control ----------------------------------------------------------------------

const char* Control::SERVICE = "org.videowallpaper.Daemon";
const char* Control::PATH = "/org/videowallpaper/Daemon";
const char* Control::INTERFACE = "org.videowallpaper.Daemon";

Control::Control(sdbus::IConnection& bus, Handlers handlers) : handlers_(std::move(handlers))
{
    object_ = sdbus::createObject(bus, PATH);
    auto& o = *object_;
    o.registerMethod("SetVideo").onInterface(INTERFACE).implementedAs(
        [this](std::string path, std::string monitor) { handlers_.setVideo(path, monitor); });
    o.registerMethod("ChooseVideo").onInterface(INTERFACE).implementedAs(
        [this](std::string monitor) { handlers_.chooseVideo(monitor); });
    o.registerMethod("SetPaused").onInterface(INTERFACE).implementedAs([this](bool paused) { handlers_.setPaused(paused); });
    o.registerMethod("SetMute").onInterface(INTERFACE).implementedAs([this](bool mute) { handlers_.setMute(mute); });
    o.registerMethod("SetVolume").onInterface(INTERFACE).implementedAs([this](int32_t volume) { handlers_.setVolume(volume); });
    o.registerMethod("SetCovered").onInterface(INTERFACE).implementedAs(
        [this](std::string monitor, bool covered) { handlers_.setCovered(monitor, covered); });
    o.registerMethod("GetMonitors").onInterface(INTERFACE).implementedAs([this] { return handlers_.monitors(); });
    o.registerMethod("Quit").onInterface(INTERFACE).implementedAs([this] { handlers_.quit(); });
    o.finishRegistration();
}

} // namespace vw
