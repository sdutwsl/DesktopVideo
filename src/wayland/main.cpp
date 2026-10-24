// VideoWallpaper for Wayland: layer-shell background surfaces drawn by libmpv,
// a StatusNotifierItem tray icon and a small D-Bus control interface.

#include "DBus.h"
#include "EventLoop.h"
#include "Session.h"
#include "Tray.h"
#include "WaylandDisplay.h"
#include "core/Config.h"
#include "core/Menu.h"
#include "core/Wallpaper.h"

#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>
#include <cerrno>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include <thread>

using namespace vw;

extern char** environ;

namespace {

void showMainWindow()
{
    pid_t pid = 0;
    char command[] = "videowallpaper-ui";
    char* args[] = {command, nullptr};
    if (posix_spawnp(&pid, command, nullptr, nullptr, args, environ) != 0)
        fputs("[ui] videowallpaper-ui is not installed; see README for GUI setup\n", stderr);
    else
        std::thread([pid] { while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {} }).detach();
}

std::string videoSource(const std::string& value)
{
    if (value.starts_with("https://") || value.starts_with("http://")) return value;
    return std::filesystem::absolute(value).string();
}

void usage()
{
    fputs("usage: videowallpaper [--config FILE] [COMMAND]\n"
          "\n"
          "  (no command)          start the wallpaper daemon\n"
          "  FILE                  use FILE as the wallpaper video (starts the daemon if needed)\n"
          "  set FILE [MONITOR]    set the video, optionally for one monitor only\n"
          "  choose [MONITOR]      open a file dialog\n"
          "  pause | resume        pause / resume playback\n"
          "  mute | unmute\n"
          "  volume N              0..100\n"
          "  monitors              list monitor names\n"
          "  quit                  stop the daemon\n",
          stderr);
}

std::string defaultConfigPath()
{
    const char* xdg = getenv("XDG_CONFIG_HOME");
    std::filesystem::path base = (xdg && *xdg) ? std::filesystem::path(xdg)
                                                : std::filesystem::path(getenv("HOME") ? getenv("HOME") : ".") / ".config";
    return (base / "videowallpaper" / "config.ini").string();
}

// Send a command to the running daemon.
int forward(sdbus::IConnection& bus, const std::vector<std::string>& args)
{
    auto proxy = sdbus::createProxy(bus, Control::SERVICE, Control::PATH);
    auto call = [&](const char* method, auto&&... a) {
        proxy->callMethod(method).onInterface(Control::INTERFACE).withArguments(std::forward<decltype(a)>(a)...);
    };
    const std::string& cmd = args.empty() ? "" : args[0];
    try {
        if (cmd.empty()) {
            fputs("videowallpaper is already running\n", stderr);
            return 1;
        } else if (cmd == "set" && args.size() >= 2) {
            call("SetVideo", videoSource(args[1]), args.size() > 2 ? args[2] : "");
        } else if (cmd == "choose") {
            call("ChooseVideo", args.size() > 1 ? args[1] : "");
        } else if (cmd == "pause" || cmd == "resume") {
            call("SetPaused", cmd == "pause");
        } else if (cmd == "mute" || cmd == "unmute") {
            call("SetMute", cmd == "mute");
        } else if (cmd == "volume" && args.size() >= 2) {
            call("SetVolume", int32_t(std::atoi(args[1].c_str())));
        } else if (cmd == "monitors") {
            std::vector<std::string> names;
            proxy->callMethod("GetMonitors").onInterface(Control::INTERFACE).storeResultsTo(names);
            for (const std::string& n : names)
                puts(n.c_str());
        } else if (cmd == "quit") {
            call("Quit");
        } else if (cmd.starts_with("https://") || cmd.starts_with("http://") || std::filesystem::is_regular_file(cmd)) {
            call("SetVideo", videoSource(cmd), std::string());
        } else {
            usage();
            return 2;
        }
    } catch (const sdbus::Error& e) {
        fprintf(stderr, "videowallpaper: %s\n", e.getMessage().c_str());
        return 1;
    }
    return 0;
}

class SignalSource : public EventLoop::Source {
public:
    explicit SignalSource(std::function<void()> onSignal) : onSignal_(std::move(onSignal))
    {
        sigset_t mask;
        sigemptyset(&mask);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGTERM);
        sigaddset(&mask, SIGHUP);
        sigprocmask(SIG_BLOCK, &mask, nullptr);
        fd_ = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    }
    ~SignalSource() override { close(fd_); }
    int fd() override { return fd_; }
    void dispatch(short revents) override
    {
        if (!(revents & 1))
            return;
        signalfd_siginfo info;
        [[maybe_unused]] ssize_t n = read(fd_, &info, sizeof info);
        onSignal_();
    }

private:
    int fd_ = -1;
    std::function<void()> onSignal_;
};

void wrapActivations(std::vector<MenuItem>& items, const std::function<void()>& after)
{
    for (MenuItem& item : items) {
        if (item.activate) {
            auto fn = item.activate;
            item.activate = [fn, after] {
                fn();
                after();
            };
        }
        wrapActivations(item.children, after);
    }
}

} // namespace

int main(int argc, char** argv)
{
    std::string configPath = defaultConfigPath();
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--config") && i + 1 < argc)
            configPath = argv[++i];
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage();
            return 0;
        } else if (!strcmp(argv[i], "--background")) {
            // Accepted on both platforms so the GUI can start the daemon uniformly.
        } else if (!strcmp(argv[i], "ui")) {
            showMainWindow();
            return 0;
        } else
            args.push_back(argv[i]);
    }

    std::unique_ptr<sdbus::IConnection> session;
    try {
        session = sdbus::createSessionBusConnection();
        session->requestName(Control::SERVICE);
    } catch (const sdbus::Error& e) {
        if (!session) {
            fprintf(stderr, "videowallpaper: cannot connect to the session bus: %s\n", e.getMessage().c_str());
            return 1;
        }
        return forward(*session, args);   // someone already owns the name: we are the client
    }

    // Commands that only make sense against a running daemon.
    std::string initialVideo;
    if (!args.empty()) {
        const std::string& cmd = args[0];
        if (cmd == "set" && args.size() >= 2 && args.size() == 2)
            initialVideo = videoSource(args[1]);
        else if (cmd.starts_with("https://") || cmd.starts_with("http://") || std::filesystem::is_regular_file(cmd))
            initialVideo = videoSource(cmd);
        else {
            fputs("videowallpaper: daemon is not running\n", stderr);
            return 1;
        }
    }

    Config config(configPath);
    config.load();
    if (!initialVideo.empty()) {
        config.set("video", initialVideo);
        config.save();
    }

    EventLoop loop;
    DBusSource sessionSource(*session);
    loop.add(&sessionSource);

    std::unique_ptr<sdbus::IConnection> system;
    std::unique_ptr<DBusSource> systemSource;
    try {
        system = sdbus::createSystemBusConnection();
        systemSource = std::make_unique<DBusSource>(*system);
        loop.add(systemSource.get());
    } catch (const sdbus::Error&) {
        fputs("[session] system bus unavailable; sleep/shutdown events disabled\n", stderr);
    }

    WaylandDisplay display(loop);
    std::string error;
    if (!display.connect(error)) {
        fprintf(stderr, "videowallpaper: %s\n", error.c_str());
        return 1;
    }

    Wallpaper wallpaper(display, config);
    Tray tray(*session, "videowallpaper", "Video Wallpaper");
    FileChooser chooser(*session);

    MenuActions actions;
    std::function<void()> rebuildMenu;
    actions.quit = [&] { loop.quit(); };
    actions.showMainWindow = showMainWindow;
    actions.chooseVideo = [&](const std::string& monitor) {
        chooser.open(monitor.empty() ? "选择壁纸视频" : "选择 " + monitor + " 的壁纸视频", [&, monitor](std::string path) {
            if (!path.empty())
                wallpaper.setVideo(path, monitor);
            rebuildMenu();
        });
    };
    rebuildMenu = [&] {
        std::vector<MenuItem> items = buildAppMenu(wallpaper, actions);
        wrapActivations(items, rebuildMenu);
        tray.setMenu(std::move(items));
    };
    tray.onActivate = [&] {
        wallpaper.setUserPaused(!wallpaper.userPaused());
        rebuildMenu();
    };

    display.onOutputsChanged = [&] {
        wallpaper.refreshMonitors();
        rebuildMenu();
    };
    display.onDisconnected = [&] {
        fputs("videowallpaper: compositor went away\n", stderr);
        loop.quit();
    };

    SessionMonitor sessionMonitor(*session, system.get());
    sessionMonitor.onLocked = [&](bool locked) { wallpaper.setLocked(locked); };
    sessionMonitor.onSleep = [&](bool sleeping) { wallpaper.setSuspended(sleeping); };
    sessionMonitor.onShutdown = [&] { loop.quit(); };

    AppControl appControl(wallpaper, config, [&] { loop.quit(); });
    Control control(*session, {
        [&](std::string path, std::string monitor) { wallpaper.setVideo(path, monitor); rebuildMenu(); },
        [&](std::string monitor) { actions.chooseVideo(monitor); },
        [&](bool paused) { wallpaper.setUserPaused(paused); rebuildMenu(); },
        [&](bool mute) { wallpaper.setMute(mute); rebuildMenu(); },
        [&](int volume) { wallpaper.setVolume(volume); },
        [&] { loop.quit(); },
        [&] {
            std::vector<std::string> names;
            for (const MonitorInfo& m : wallpaper.monitors())
                names.push_back(m.id);
            return names;
        },
    }, appControl);

    SignalSource signals([&] { loop.quit(); });
    loop.add(&signals);

    wallpaper.refreshMonitors();
    rebuildMenu();
    if (wallpaper.videoFor("").empty())
        fputs("[wallpaper] no video configured yet; pick one from the tray menu or run: videowallpaper set FILE\n", stderr);

    loop.run();

    wallpaper.shutdown();
    return 0;
}
