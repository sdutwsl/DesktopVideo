#include "core/AppControl.h"
#include <cassert>
#include <filesystem>
#include <iostream>

#ifdef _WIN32
#include "win/ControlPipe.h"
#else
#include "wayland/Session.h"
#endif

using namespace muralop;
struct TestSurface : Surface {
    MonitorInfo info;
    explicit TestSurface(MonitorInfo value) : info(std::move(value)) {}
    const MonitorInfo& monitor() const override { return info; }
    void attach(MpvPlayer&) override {}
    void detach() override {}
};
struct TestPlatform : Platform {
    int created = 0;
    std::vector<MonitorInfo> monitors() override { return {{"screen-a", 0, 0, 1920, 1080, true}, {"screen-b", 1920, 0, 2560, 1440, false}}; }
    std::unique_ptr<Surface> createSurface(const MonitorInfo& monitor) override { ++created; return std::make_unique<TestSurface>(monitor); }
};

#ifdef _WIN32
static ControlPipe* activePipe = nullptr;
LRESULT CALLBACK testWindow(HWND window, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == ControlPipe::Message) { activePipe->dispatch(); return 0; }
    if (message == WM_CLOSE) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wp, lp);
}
#endif

int main(int argc, char** argv)
{
    if (argc > 1 && std::string(argv[1]) == "rpc") {
        try {
            char input[32769];
            std::cin.read(input, sizeof input);
            const auto count = std::cin.gcount();
            if (count > 32768) return 1;
#ifdef _WIN32
            std::cout << ControlPipe::request(std::string(input, count)) << '\n';
#else
            std::cout << Control::request(std::string(input, count)) << '\n';
#endif
            return 0;
        } catch (const std::exception&) { return 1; }
    }
    auto path = std::filesystem::temp_directory_path() / "muralop-control-test.ini";
    Config config(path.string());
    TestPlatform platform;
    Wallpaper wallpaper(platform, config);
    wallpaper.refreshMonitors();
    bool quit = false;
    AppControl control(wallpaper, config, [&] { quit = true; });
#ifdef _WIN32
    if (argc > 1 && std::string(argv[1]) == "--serve") {
        WNDCLASSW cls{};
        cls.lpfnWndProc = testWindow;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"MuralopPipeTest";
        RegisterClassW(&cls);
        HWND window = CreateWindowW(cls.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, cls.hInstance, nullptr);
        AppControl serverControl(wallpaper, config, [&] { PostMessageW(window, WM_CLOSE, 0, 0); });
        {
            ControlPipe pipe(window, serverControl);
            activePipe = &pipe;
            MSG message;
            while (GetMessageW(&message, nullptr, 0, 0) > 0) DispatchMessageW(&message);
        }
        DestroyWindow(window);
        std::filesystem::remove(path);
        return 0;
    }
#else
    if (argc > 1 && std::string(argv[1]) == "--serve") {
        auto bus = sdbus::createSessionBusConnection();
        bus->requestName(sdbus::ServiceName(Control::SERVICE));
        AppControl serverControl(wallpaper, config, [&] { bus->leaveEventLoop(); });
        Control server(*bus, {}, serverControl);
        bus->enterEventLoop();
        std::filesystem::remove(path);
        return 0;
    }
#endif
    auto ok = [&](const std::string& command) { return control.execute(command).find("\"ok\":true") != std::string::npos; };
    assert(ok("set\nhttps://www.youtube.com/watch?v=abcdefghijk\n"));
    assert(wallpaper.videoFor("screen-a").starts_with("https://"));
    assert(ok("set\nother.mp4\nscreen-b"));
    assert(ok("set\nnew.mp4\n"));
    assert(wallpaper.videoFor("screen-b") == "other.mp4");
    assert(!ok("set\nx\nmissing-monitor"));
    assert(ok("clear\nscreen-b"));
    assert(wallpaper.videoFor("screen-b") == "new.mp4");
    assert(ok("volume\n37") && wallpaper.volume() == 37);
    assert(!ok("volume\n37garbage") && !ok("volume\n101"));
    assert(ok("pause") && wallpaper.userPaused());
    assert(ok("resume") && !wallpaper.userPaused());
    assert(!ok("pause\nquit"));
    const int count = platform.created;
    assert(ok("online\nC:\\Program Files\\yt-dlp.exe\nC:\\a,b\\cookies.txt\n"));
    assert(platform.created == count); // Settings must not destroy a resolving player.
    assert(ok("online\nC:\\Program Files\\yt-dlp.exe\nC:\\a,b\\cookies.txt\n"));
    assert(platform.created == count); // Unchanged settings must not restart playback.
    assert(config.get("cookies_file") == "C:\\a,b\\cookies.txt");
    assert(ok("play-resolved\nhttps://www.youtube.com/watch?v=abcdefghijk\nhttps://media.example/video\nhttps://media.example/audio\nExample agent\nhttps://www.youtube.com/\n"));
    assert(wallpaper.videoFor("") == "https://www.youtube.com/watch?v=abcdefghijk");
    assert(config.get("video").find("media.example") == std::string::npos);
    assert(!ok("play-resolved\nhttps://youtube.com/\nfile:///secret\n\n\n\n"));
    assert(ok("stop\n"));
    assert(wallpaper.playbackState() == "idle");
    assert(!ok(std::string("set\nx\0quit\n", 12)));
    assert(ok("quit") && quit);
    std::filesystem::remove(path);
    std::cout << "controller tests passed\n";
}
