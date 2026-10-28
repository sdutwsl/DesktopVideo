// Stable Node-API entry point. Electron loads this in its playback utility
// process; every desktop surface and mpv handle belongs to the render thread.
#ifdef _WIN32
// Resolve Node-API from the host executable, including Electron's renamed exe.
// This avoids linking a node.exe import library into an Electron addon.
#define NAPI_EXTERN
#endif
#include <node_api.h>

#include "core/AppControl.h"
#include "core/Config.h"
#include "core/Wallpaper.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <future>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#ifdef _WIN32
#include "win/DesktopSurface.h"
#include <wtsapi32.h>
#else
#include "wayland/DBus.h"
#include "wayland/Session.h"
#include "wayland/WaylandDisplay.h"
#endif

namespace muralop {
namespace {

#ifdef _WIN32
struct NodeApi {
#define API_FIELD(name) decltype(&napi_##name) name = nullptr;
    API_FIELD(get_cb_info)
    API_FIELD(get_value_string_utf8)
    API_FIELD(create_string_utf8)
    API_FIELD(get_undefined)
    API_FIELD(throw_error)
    API_FIELD(define_properties)
    API_FIELD(set_instance_data)
    API_FIELD(get_instance_data)
#undef API_FIELD
    bool load()
    {
        HMODULE host = GetModuleHandleW(nullptr);
#define API_LOAD(name) name = reinterpret_cast<decltype(name)>(GetProcAddress(host, "napi_" #name)); if (!name) return false;
        API_LOAD(get_cb_info)
        API_LOAD(get_value_string_utf8)
        API_LOAD(create_string_utf8)
        API_LOAD(get_undefined)
        API_LOAD(throw_error)
        API_LOAD(define_properties)
        API_LOAD(set_instance_data)
        API_LOAD(get_instance_data)
#undef API_LOAD
        return true;
    }
} api;
#define napi_get_cb_info api.get_cb_info
#define napi_get_value_string_utf8 api.get_value_string_utf8
#define napi_create_string_utf8 api.create_string_utf8
#define napi_get_undefined api.get_undefined
#define napi_throw_error api.throw_error
#define napi_define_properties api.define_properties
#define napi_set_instance_data api.set_instance_data
#define napi_get_instance_data api.get_instance_data
#endif

class Player {
    struct Command {
        std::string payload;
        std::promise<std::string> result;
    };
public:
    ~Player()
    {
        try { if (running()) request("quit"); } catch (...) {}
        if (thread_.joinable()) thread_.join();
    }

    std::string start(std::string configPath, std::string legacyPath)
    {
        auto ready = ready_.get_future();
        thread_ = std::thread([this, configPath = std::move(configPath), legacyPath = std::move(legacyPath)] {
            try {
                Config config(configPath, legacyPath);
                config.load();
                run(config);
                deactivate("Playback stopped");
            } catch (const std::exception& error) {
                if (!activated_) ready_.set_exception(std::current_exception());
                deactivate(error.what(), true);
            } catch (...) {
                if (!activated_) ready_.set_exception(std::current_exception());
                deactivate("Playback thread stopped unexpectedly", true);
            }
        });
        return ready.get();
    }

    bool running()
    {
        std::lock_guard lock(mutex_);
        return bool(wake_);
    }

    std::string request(const std::string& payload)
    {
        auto command = std::make_shared<Command>();
        command->payload = payload;
        auto reply = command->result.get_future();
        {
            std::lock_guard lock(mutex_);
            if (!wake_) {
                if (!failure_.empty()) throw std::runtime_error(failure_);
                if (payload == "status") return "{\"ok\":true,\"stopped\":true}";
                throw std::runtime_error("Playback is not running");
            }
            commands_.push_back(command);
            wake_();
        }
        return reply.get();
    }

private:
    void activate(std::function<void()> wake, AppControl& control)
    {
        {
            std::lock_guard lock(mutex_);
            wake_ = std::move(wake);
        }
        ready_.set_value(control.state());
        activated_ = true;
    }

    void deactivate(const char* message, bool failed = false)
    {
        std::lock_guard lock(mutex_);
        wake_ = {};
        if (failed) failure_ = message;
        for (auto& command : commands_) {
            if (!failed && command->payload == "status")
                command->result.set_value("{\"ok\":true,\"stopped\":true}");
            else command->result.set_exception(std::make_exception_ptr(std::runtime_error(message)));
        }
        commands_.clear();
    }

    void dispatch(AppControl& control)
    {
        std::vector<std::shared_ptr<Command>> pending;
        {
            std::lock_guard lock(mutex_);
            pending.swap(commands_);
        }
        for (auto& command : pending) {
            try { command->result.set_value(control.execute(command->payload)); }
            catch (...) { command->result.set_exception(std::current_exception()); }
        }
    }

    // Clear cross-thread callbacks before the objects they reference are freed.
    struct Guard {
        Player& player;
        int exceptions = std::uncaught_exceptions();
        ~Guard() { player.deactivate("Playback stopped", std::uncaught_exceptions() > exceptions); }
    };

#ifdef _WIN32
    static constexpr UINT CommandMessage = WM_APP + 37;
    struct Window {
        Player* player;
        Wallpaper* wallpaper;
        WinPlatform* platform;
        AppControl* control;
        UINT taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    };

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* context = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            context = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
        }
        if (!context) return DefWindowProcW(hwnd, message, wParam, lParam);
        if (message == context->taskbarCreated || message == WM_DISPLAYCHANGE) {
            context->platform->invalidateWorkerW();
            SetTimer(hwnd, 1, 1500, nullptr);
            return 0;
        }
        switch (message) {
        case CommandMessage: context->player->dispatch(*context->control); return 0;
        case WM_TIMER:
            KillTimer(hwnd, 1);
            context->wallpaper->rebuildSurfaces();
            return 0;
        case WM_WTSSESSION_CHANGE:
            if (wParam == WTS_SESSION_LOCK) context->wallpaper->setLocked(true);
            else if (wParam == WTS_SESSION_UNLOCK) context->wallpaper->setLocked(false);
            return 0;
        case WM_POWERBROADCAST:
            if (wParam == PBT_APMSUSPEND) context->wallpaper->setSuspended(true);
            else if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)
                context->wallpaper->setSuspended(false);
            return TRUE;
        case WM_QUERYENDSESSION: return TRUE;
        case WM_ENDSESSION: if (wParam) PostQuitMessage(0); return 0;
        case WM_CLOSE: PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void run(Config& config)
    {
        // DPI is local to this render thread, not a system preference.
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        WinPlatform platform;
        Wallpaper wallpaper(platform, config);
        AppControl control(wallpaper, config, [] { PostQuitMessage(0); });
        Window context{this, &wallpaper, &platform, &control};
        const auto instance = GetModuleHandleW(nullptr);
        WNDCLASSW cls{};
        cls.hInstance = instance;
        cls.lpfnWndProc = windowProc;
        cls.lpszClassName = L"MuralopNodePlayer";
        if (!RegisterClassW(&cls)) throw std::runtime_error("Cannot register playback window");
        HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, cls.lpszClassName, L"Muralop Playback", WS_OVERLAPPED,
            0, 0, 0, 0, nullptr, nullptr, instance, &context);
        if (!hwnd) {
            UnregisterClassW(cls.lpszClassName, instance);
            throw std::runtime_error("Cannot create playback window");
        }
        struct WindowGuard {
            HWND hwnd; HINSTANCE instance; const wchar_t* name;
            ~WindowGuard() { WTSUnRegisterSessionNotification(hwnd); DestroyWindow(hwnd); UnregisterClassW(name, instance); }
        } windowGuard{hwnd, instance, cls.lpszClassName};
        WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION);
        Guard guard{*this};
        wallpaper.refreshMonitors();
        activate([hwnd] { PostMessageW(hwnd, CommandMessage, 0, 0); }, control);
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        deactivate("Playback stopped");
        wallpaper.shutdown();
    }
#else
    void run(Config& config)
    {
        EventLoop loop;
        // Observe session events and retain CLI control without a second tray.
        std::unique_ptr<sdbus::IConnection> session, system;
        std::unique_ptr<DBusSource> sessionSource, systemSource;
        try {
            session = sdbus::createSessionBusConnection();
            sessionSource = std::make_unique<DBusSource>(loop, *session);
        } catch (const sdbus::Error&) {}
        if (session) {
            try { session->requestName(sdbus::ServiceName(Control::SERVICE)); }
            catch (const sdbus::Error&) {
                throw std::runtime_error("已有独立 Muralop 实例占用播放接口，请先从旧实例托盘退出");
            }
        }
        try {
            system = sdbus::createSystemBusConnection();
            systemSource = std::make_unique<DBusSource>(loop, *system);
        } catch (const sdbus::Error&) {}

        WaylandDisplay display(loop);
        std::string error;
        if (!display.connect(error)) throw std::runtime_error(error);
        Wallpaper wallpaper(display, config);
        AppControl control(wallpaper, config, [&] { loop.quit(); });
        std::unique_ptr<SessionMonitor> monitor;
        std::unique_ptr<FileChooser> chooser;
        std::unique_ptr<Control> remote;
        if (session) {
            chooser = std::make_unique<FileChooser>(*session);
            remote = std::make_unique<Control>(*session, Control::Handlers{
                [&](std::string path, std::string target) { wallpaper.setVideo(path, target); },
                [&](std::string target) {
                    chooser->open("选择壁纸视频", [&, target](std::string path) {
                        if (!path.empty()) wallpaper.setVideo(path, target);
                    });
                },
                [&](bool paused) { wallpaper.setUserPaused(paused); },
                [&](bool mute) { wallpaper.setMute(mute); },
                [&](int volume) { wallpaper.setVolume(volume); },
                [&] { loop.quit(); },
                [&] {
                    std::vector<std::string> names;
                    for (const auto& item : wallpaper.monitors()) names.push_back(item.id);
                    return names;
                },
            }, control);
            monitor = std::make_unique<SessionMonitor>(*session, system.get());
            monitor->onLocked = [&](bool locked) { wallpaper.setLocked(locked); };
            monitor->onSleep = [&](bool sleeping) { wallpaper.setSuspended(sleeping); };
            monitor->onShutdown = [&] { loop.quit(); };
        }
        display.onOutputsChanged = [&] { wallpaper.refreshMonitors(); };
        display.onDisconnected = [&] { loop.quit(); };
        Guard guard{*this};
        wallpaper.refreshMonitors();
        activate([&] { loop.post([&] { dispatch(control); }); }, control);
        loop.run();
        deactivate("Playback stopped");
        wallpaper.shutdown();
    }
#endif

    std::mutex mutex_;
    std::vector<std::shared_ptr<Command>> commands_;
    std::function<void()> wake_;
    std::string failure_;
    std::promise<std::string> ready_;
    std::thread thread_;
    bool activated_ = false; // only accessed on the render thread
};

struct Instance { std::unique_ptr<Player> player; };

std::string stringArgument(napi_env env, napi_value value)
{
    size_t size = 0;
    if (!value || napi_get_value_string_utf8(env, value, nullptr, 0, &size) != napi_ok || size > 32768)
        throw std::runtime_error("Invalid playback argument");
    std::vector<char> buffer(size + 1);
    if (napi_get_value_string_utf8(env, value, buffer.data(), buffer.size(), &size) != napi_ok)
        throw std::runtime_error("Invalid playback argument");
    std::string result(buffer.data(), size);
    if (result.find('\0') != std::string::npos) throw std::runtime_error("Invalid playback argument");
    return result;
}

napi_value call(napi_env env, napi_callback_info info)
{
    try {
        napi_value args[2]{};
        size_t count = 2;
        void* method = nullptr;
        napi_get_cb_info(env, info, &count, args, nullptr, &method);
        void* data = nullptr;
        napi_get_instance_data(env, &data);
        auto* instance = static_cast<Instance*>(data);
        if (!instance) throw std::runtime_error("Playback plugin is not initialized");
        std::string result;
        const auto operation = reinterpret_cast<intptr_t>(method);
        if (operation == 1) {
            if (instance->player) throw std::runtime_error("Playback already started");
            auto player = std::make_unique<Player>();
            result = player->start(stringArgument(env, args[0]), count > 1 ? stringArgument(env, args[1]) : "");
            instance->player = std::move(player);
        } else if (operation == 2) {
            if (!instance->player) throw std::runtime_error("Playback is not running");
            result = instance->player->request(stringArgument(env, args[0]));
        } else {
            instance->player.reset();
            napi_value undefined;
            napi_get_undefined(env, &undefined);
            return undefined;
        }
        napi_value value;
        napi_create_string_utf8(env, result.data(), result.size(), &value);
        return value;
    } catch (const std::exception& error) {
        napi_throw_error(env, nullptr, error.what());
        return nullptr;
    }
}

napi_value init(napi_env env, napi_value exports)
{
#ifdef _WIN32
    if (!api.load()) return nullptr;
#endif
    napi_set_instance_data(env, new Instance, [](napi_env, void* data, void*) { delete static_cast<Instance*>(data); }, nullptr);
    const napi_property_descriptor properties[] = {
        {"start", nullptr, call, nullptr, nullptr, nullptr, napi_default, reinterpret_cast<void*>(1)},
        {"request", nullptr, call, nullptr, nullptr, nullptr, napi_default, reinterpret_cast<void*>(2)},
        {"close", nullptr, call, nullptr, nullptr, nullptr, napi_default, nullptr},
    };
    napi_define_properties(env, exports, std::size(properties), properties);
    return exports;
}

} // namespace
} // namespace muralop

NAPI_MODULE(muralop_player, muralop::init)
